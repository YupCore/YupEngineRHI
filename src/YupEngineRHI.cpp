#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/vfs/ZipFile.h>

#include "FXAA.h"
#include "FullScreenYUV.h"
#include "UIRenderer.h"
#include "RenderTargets.h"
#include "AudioSource.h"
#include "VideoRenderer.h"

#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/SkyPass.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/render/CascadedShadowMap.h>
#include <donut/render/BloomPass.h>
#include <donut/shaders/material_cb.h>
#include <donut/shaders/bindless.h>
#include <donut/app/Camera.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>
#include <donut/render/DepthPass.h>
#include <donut/engine/Scene.h>
#include <bitset>

#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif
#include <donut/render/ToneMappingPasses.h>
#include <donut/render/LightProbeProcessingPass.h>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

static const char* g_WindowTitle = "YupEngine";

class YupEngine : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    std::shared_ptr<ShaderFactory>          m_ShaderFactory;
    //std::unique_ptr<engine::BindingCache> m_Popugay_Cache; // Настюша Гаражик спс за идею
    std::unique_ptr<engine::BindingCache>   m_BindingCache;
    std::shared_ptr<InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::shared_ptr<TransparentDrawStrategy> m_TransparentDrawStrategy;

    std::shared_ptr<RenderTargets>          m_RenderTargets;

    // Пасы
    std::unique_ptr<GBufferFillPass>        m_GBufferPass;
    std::unique_ptr<ForwardShadingPass>     m_ForwardPass;
    std::unique_ptr<DeferredLightingPass>   m_DeferredLightingPass;
    std::unique_ptr<SkyPass>                m_SkyPass;
    std::unique_ptr<BloomPass>              m_BloomPass;
    std::unique_ptr<SsaoPass>               m_SsaoPass;
    std::unique_ptr<FXAAPass>               m_FXAAPass;
    std::unique_ptr<ToneMappingPass>        m_ToneMappingPass;
    std::unique_ptr<MaterialIDPass>         m_MaterialIDPass;
    std::unique_ptr<LightProbeProcessingPass> m_LightProbePass;
    std::shared_ptr<FullScreenYUVPass>      m_YUVPass;
    std::unique_ptr<VideoRenderer>          m_VideoRenderer;

    // Свет и тени
    std::shared_ptr<DirectionalLight>       m_SunLight;
    std::shared_ptr<CascadedShadowMap>      m_ShadowMap;
    std::shared_ptr<FramebufferFactory>     m_ShadowFramebuffer;
    std::shared_ptr<DepthPass>              m_ShadowDepthPass;

    // чета
    std::shared_ptr<PlanarView>             m_View;
    std::shared_ptr<PlanarView>             m_ViewPrevious;

    std::vector<std::string>                m_SceneFilesAvailable;
    std::string                             m_CurrentSceneName;
    std::filesystem::path                   m_SceneDir;
    std::shared_ptr<Scene>				    m_Scene;


    donut::app::FirstPersonCamera           m_Camera;
    nvrhi::CommandListHandle                m_CommandList;
    GLFWwindow*                             window;
    float                                   m_CameraVerticalFov = 60.f;
    float3                                  m_AmbientTop = 0.f;
    float3                                  m_AmbientBottom = 0.f;
    bool                                    m_PreviousViewsValid = false;
    std::shared_ptr<vfs::RootFileSystem>    rootFS;
    std::shared_ptr<vfs::ZipFile>           m_ZipFS;
    float                                   m_WallclockTime = 0.f;

    std::vector<std::shared_ptr<LightProbe>> m_LightProbes;
    nvrhi::TextureHandle                    m_LightProbeDiffuseTexture;
    nvrhi::TextureHandle                    m_LightProbeSpecularTexture;

    UIData&                                 m_ui;
    std::shared_ptr<AudioSource>            m_source3D;
    bool                                    m_skipSplash = false;

public:

    YupEngine(DeviceManager* dvm, UIData& ui)
        : Super(dvm),
        m_ui(ui)
    {
        SetAsynchronousLoadingEnabled(true);

        m_ZipFS = std::make_shared<vfs::ZipFile>(app::GetDirectoryWithExecutable() / "Assets.zip");

        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "Shaders" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        rootFS = std::make_shared<vfs::RootFileSystem>();
        rootFS->mount("/shaders", frameworkShaderPath);
        rootFS->mount("/", m_ZipFS);
        m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), rootFS, "/shaders");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);
        m_BindingCache = std::make_unique<engine::BindingCache>(GetDevice());

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();
        m_TransparentDrawStrategy = std::make_shared<TransparentDrawStrategy>();

        m_DeferredLightingPass = std::make_unique<DeferredLightingPass>(GetDevice(), m_CommonPasses);
        m_DeferredLightingPass->Init(m_ShaderFactory);

        m_TextureCache = std::make_shared<TextureCache>(GetDevice(), m_ZipFS, nullptr);

        const nvrhi::Format shadowMapFormats[] = {
           nvrhi::Format::D24S8,
           nvrhi::Format::D32,
           nvrhi::Format::D16,
           nvrhi::Format::D32S8 
        };

        const nvrhi::FormatSupport shadowMapFeatures =
            nvrhi::FormatSupport::Texture       |
            nvrhi::FormatSupport::DepthStencil  |
            nvrhi::FormatSupport::ShaderLoad;

        nvrhi::Format shadowMapFormat = nvrhi::utils::ChooseFormat(GetDevice(), shadowMapFeatures, shadowMapFormats, std::size(shadowMapFormats));

        m_ShadowMap = std::make_shared<CascadedShadowMap>(GetDevice(), 2048, 4, 0, shadowMapFormat);
        m_ShadowMap->SetupProxyViews();

        m_ShadowFramebuffer = std::make_shared<FramebufferFactory>(GetDevice());
        m_ShadowFramebuffer->DepthTarget = m_ShadowMap->GetTexture();

        DepthPass::CreateParameters shadowDepthParams;
        shadowDepthParams.slopeScaledDepthBias = 4.f;
        shadowDepthParams.depthBias = 100;
        m_ShadowDepthPass = std::make_shared<DepthPass>(GetDevice(), m_CommonPasses);
        m_ShadowDepthPass->Init(*m_ShaderFactory, shadowDepthParams);

        m_CommandList = GetDevice()->createCommandList();

        window = GetDeviceManager()->GetWindow();

        m_Camera.SetMoveSpeed(3.0f);
        m_Camera.LookAt(
            float3(0.f, 0.0f, -3.f),
            float3(1.f, 0.f, 0.f));
        m_CameraVerticalFov = 60.f;

        m_ui.SkyParams.brightness = 0.776f;
        m_ui.SkyParams.glowSize = 20;
        m_ui.SkyParams.glowSharpness = 4;
        m_ui.SkyParams.glowIntensity = 0.1f;
        m_ui.SkyParams.horizonSize = 25;
        m_PreviousViewsValid = false;
        m_Camera.stopRotation = true;

        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);

        uint sampleCount = 1;

        bool needNewPasses = false;

        if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(uint2(windowWidth, windowHeight), sampleCount))
        {
            m_RenderTargets = nullptr;
            m_BindingCache->Clear();
            m_RenderTargets = std::make_unique<RenderTargets>();
            m_RenderTargets->Init(GetDevice(), uint2(windowWidth, windowHeight), sampleCount, true, true);

            needNewPasses = true;
        }

        SetupView();

        bool nop;
        CreateRenderPasses(nop);

        AudioEngine::InitEngine(m_ZipFS);
        m_VideoRenderer = std::make_unique<VideoRenderer>(GetDevice(), m_ZipFS, "Videos/Intro_Test.mkv");
        GetDeviceManager()->SetEnableRenderDuringWindowMovement(true);

        m_SceneDir = "Models";
        m_SceneFilesAvailable = FindScenes(*m_ZipFS, m_SceneDir);
        SetCurrentSceneName(app::FindPreferredScene(m_SceneFilesAvailable, "MegaScena.gltf"));

    }

    std::filesystem::path const& GetSceneDir() const
    {
        return m_SceneDir;
    }

    std::string GetCurrentSceneName() const
    {
        return m_CurrentSceneName;
    }

    void SetCurrentSceneName(const std::string& sceneName)
    {
        if (m_CurrentSceneName == sceneName)
            return;

        m_CurrentSceneName = sceneName;

        BeginLoadingScene(m_ZipFS, m_CurrentSceneName);
    }

    bool SetupView()
    {
        float2 renderTargetSize = float2(m_RenderTargets->GetSize());

        affine3 viewMatrix = m_Camera.GetWorldToViewMatrix();

        bool topologyChanged = false;

        if (!m_View)
        {
            m_View = std::make_shared<PlanarView>();
            m_ViewPrevious = std::make_shared<PlanarView>();
            topologyChanged = true;
        }

        float verticalFov = dm::radians(m_CameraVerticalFov);
        float4x4 projection = perspProjD3DStyleReverse(verticalFov, renderTargetSize.x / renderTargetSize.y, 0.01f);

        m_View->SetViewport(nvrhi::Viewport(renderTargetSize.x, renderTargetSize.y));
        m_View->SetMatrices(viewMatrix, projection);

        if (topologyChanged)
        {
            m_ViewPrevious = m_View;
        }

        m_View->UpdateCache();
        m_ViewPrevious->UpdateCache();
        m_PreviousViewsValid = false;
        return topologyChanged;
    }

    std::shared_ptr<vfs::RootFileSystem> GetRootFS()
    {
        return rootFS;
    }

    virtual void SceneUnloading() override
    {
        if (!m_VideoRenderer->EOV)
        {
            //m_VideoRenderer->UninitFFMPEG();
            SetSplashScreenFinished(true);
        }

        if (m_ForwardPass) m_ForwardPass->ResetBindingCache();
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        if (m_LightProbePass) m_LightProbePass->ResetCaches();
        if (m_ShadowDepthPass) m_ShadowDepthPass->ResetBindingCache();
        m_BindingCache->Clear();
        m_SunLight.reset();
        m_ui.SceneLoadedStatus = false;
        m_skipSplash = false;

        for (auto probe : m_LightProbes)
        {
            probe->enabled = false;
        }
    }

    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override
    {
        using namespace std::chrono;

        std::unique_ptr<engine::Scene> scene = std::make_unique<engine::Scene>(GetDevice(),
            *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

        auto startTime = high_resolution_clock::now();

        if (scene->Load(fileName))
        {
            m_Scene = std::move(scene);

            auto endTime = high_resolution_clock::now();
            auto duration = duration_cast<milliseconds>(endTime - startTime).count();
            log::info("Scene loading time: %llu ms", duration);
            SetSplashScreenFinished(true);

            return true;
        }

        return false;
    }

    virtual void SceneLoaded() override
    {
        Super::SceneLoaded();

        m_ui.SceneLoadedStatus = true;

        m_Scene->FinishedLoading(GetFrameIndex());

        m_WallclockTime = 0.f;
        m_PreviousViewsValid = false;

        for (auto light : m_Scene->GetSceneGraph()->GetLights())
        {
            if (light->GetLightType() == LightType_Directional)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                break;
            }
        }

        if (!m_SunLight)
        {
            m_SunLight = std::make_shared<DirectionalLight>();
            m_SunLight->angularSize = 0.53f;
            m_SunLight->irradiance = 7.5f;

            auto node = std::make_shared<SceneGraphNode>();
            node->SetLeaf(m_SunLight);
            m_SunLight->SetDirection(dm::double3(0.1, -0.9, 0.1));
            m_SunLight->SetName("Sun");
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
        }

        CreateLightProbes(4);

        auto audioSourceNode = std::make_shared<SceneGraphNode>();
        audioSourceNode->SetName("AudioSource2D");
        m_source3D = std::make_shared<AudioSource>("Sounds/StarRail_Science Fiction.ogg", 1.f, true, false, 2.f);
        audioSourceNode->SetLeaf(m_source3D);

        auto sceneGraph = m_Scene->GetSceneGraph();
        sceneGraph->Attach(sceneGraph->GetRootNode(), audioSourceNode);
        m_source3D->Play();

        m_ui.lights = GetScene()->GetSceneGraph()->GetLights();
        m_ui.LightProbes = GetLightProbes();
        m_ui.m_RenderCallback = [this](LightProbe& probe) { RenderLightProbe(probe); };

        PrintSceneGraph(m_Scene->GetSceneGraph()->GetRootNode());
    }

    void Animate(float seconds) override
    {
        m_Camera.Animate(seconds);
        GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle);

        if (m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(seconds);

        if (m_ui.SceneLoadedStatus)
        {
            m_WallclockTime += seconds;
            AudioEngine::SetListenerAttributes(m_Camera.GetPosition(), -m_Camera.GetDir(), m_Camera.GetUp());

            for (const auto& anim : m_Scene->GetSceneGraph()->GetAnimations())
            {
                float duration = anim->GetDuration();
                float integral;
                float animationTime = std::modf(m_WallclockTime / duration, &integral) * duration;
                (void)anim->Apply(animationTime);
            }

            // Spinning motion for m_Source2D
            const float radius = 5.0f;
            const float speed = 1.0f; // Speed of rotation (radians per second)

            // Compute the new position
            float angle = m_WallclockTime * speed;
            double x = radius * std::cos(angle);
            double z = radius * std::sin(angle);

            // Update the position of m_Source2D
            m_source3D->GetNodeSharedPtr()->SetTranslation(double3(x, 0.0, z));
            m_source3D->Update3DAttributes();
            AudioEngine::m_system->update();
        }
    }

    void BackBufferResizing() override
    {

    }

    void CreateRenderPasses(bool& exposureResetRequired)
    {
        uint32_t motionVectorStencilMask = 0x01;

        ForwardShadingPass::CreateParameters ForwardParams;
        ForwardParams.trackLiveness = false;
        m_ForwardPass = std::make_unique<ForwardShadingPass>(GetDevice(), m_CommonPasses);
        m_ForwardPass->Init(*m_ShaderFactory, ForwardParams);

        GBufferFillPass::CreateParameters GBufferParams;
        GBufferParams.enableMotionVectors = true;
        GBufferParams.stencilWriteMask = motionVectorStencilMask;
        m_GBufferPass = std::make_unique<GBufferFillPass>(GetDevice(), m_CommonPasses);
        m_GBufferPass->Init(*m_ShaderFactory, GBufferParams);
        GBufferParams.enableMotionVectors = false;

        m_MaterialIDPass = std::make_unique<MaterialIDPass>(GetDevice(), m_CommonPasses);
        m_MaterialIDPass->Init(*m_ShaderFactory, GBufferParams);

        m_DeferredLightingPass = std::make_unique<DeferredLightingPass>(GetDevice(), m_CommonPasses);
        m_DeferredLightingPass->Init(m_ShaderFactory);

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);

        if (m_RenderTargets->GetSampleCount() == 1)
        {
            m_SsaoPass = std::make_unique<SsaoPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->Depth, m_RenderTargets->GBufferNormals, m_RenderTargets->AmbientOcclusion);
        }

        m_LightProbePass = std::make_unique<LightProbeProcessingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses);

        nvrhi::BufferHandle exposureBuffer = nullptr;
        if (m_ToneMappingPass)
            exposureBuffer = m_ToneMappingPass->GetExposureBuffer();
        else
            exposureResetRequired = true;

        ToneMappingPass::CreateParameters toneMappingParams;
        toneMappingParams.exposureBufferOverride = exposureBuffer;
        m_ToneMappingPass = std::make_unique<ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, *m_View, toneMappingParams);

        m_BloomPass = std::make_unique<BloomPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ResolvedFramebuffer, *m_View);
        m_FXAAPass = std::make_unique<FXAAPass>(
            GetDevice(),
            m_ShaderFactory,
            m_CommonPasses,
            m_RenderTargets->HdrFramebuffer,
            *m_View
        );
        m_YUVPass = std::make_shared<FullScreenYUVPass>(
            GetDevice(),
            m_ShaderFactory,
            m_CommonPasses,
            m_RenderTargets->HdrFramebuffer,
            *m_View
        );

        m_PreviousViewsValid = false;
    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        if (!(IsSceneLoaded() && m_skipSplash))
        {
            int windowWidth, windowHeight;
            GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);

            bool exposureResetRequired = false;

            {
                uint sampleCount = 1;
                bool needNewPasses = false;

                if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(uint2(windowWidth, windowHeight), sampleCount))
                {
                    m_RenderTargets = nullptr;
                    m_BindingCache->Clear();
                    m_RenderTargets = std::make_unique<RenderTargets>();
                    m_RenderTargets->Init(GetDevice(), uint2(windowWidth, windowHeight), sampleCount, true, true);

                    needNewPasses = true;
                }

                if (SetupView())
                {
                    needNewPasses = true;
                }
                if (needNewPasses)
                {
                    CreateRenderPasses(exposureResetRequired);
                }
            }

            m_CommandList->open();
            m_VideoRenderer->PresentFrame(m_RenderTargets->HdrFramebuffer, m_CommandList);
            m_YUVPass->Render(m_CommandList, m_RenderTargets->HdrFramebuffer, *m_View, m_VideoRenderer->m_dynamicYUVSource);
            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->HdrColor, m_BindingCache.get());
            m_CommandList->close();

            GetDevice()->executeCommandList(m_CommandList);
            AudioEngine::m_system->update();
        }

        if (m_VideoRenderer->EOV || (IsSceneLoaded() && m_skipSplash))
        {
            //m_VideoRenderer->UninitFFMPEG();
            SetSplashScreenFinished(true);
        }
    }


    void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));
        nvrhi::Viewport renderViewport = windowViewport;

        m_Scene->RefreshSceneGraph(GetFrameIndex());

        bool exposureResetRequired = false;

        {
            uint width = windowWidth;
            uint height = windowHeight;

            uint sampleCount = 1;

            bool needNewPasses = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(uint2(width, height), sampleCount))
            {
                m_RenderTargets = nullptr;
                m_BindingCache->Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(GetDevice(), uint2(width, height), sampleCount, true, true);

                needNewPasses = true;
            }

            if (SetupView())
            {
                needNewPasses = true;
            }

            if (m_ui.ShaderReloadRequested)
            {
                m_ShaderFactory->ClearCache();
                needNewPasses = true;
            }

            if (needNewPasses)
            {
                CreateRenderPasses(exposureResetRequired);
            }

            m_ui.ShaderReloadRequested = false;
        }

        m_CommandList->open();

        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());

        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        m_AmbientTop = m_ui.AmbientIntensity * m_ui.SkyParams.skyColor * m_ui.SkyParams.brightness;
        m_AmbientBottom = m_ui.AmbientIntensity * m_ui.SkyParams.groundColor * m_ui.SkyParams.brightness;
        if (m_ui.EnableShadows)
        {
            m_SunLight->shadowMap = m_ShadowMap;
            box3 sceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();

            frustum projectionFrustum = m_View->GetProjectionFrustum();
            const float maxShadowDistance = 1500.f;

            dm::affine3 viewMatrixInv = m_View->GetChildView(ViewType::PLANAR, 0)->GetInverseViewMatrix();

            float zRange = length(sceneBounds.diagonal()) * 0.5f;
            m_ShadowMap->SetupForPlanarViewStable(*m_SunLight, projectionFrustum, viewMatrixInv, maxShadowDistance, zRange, zRange, m_ui.CsmExponent);

            m_ShadowMap->Clear(m_CommandList);

            DepthPass::Context context;

            RenderCompositeView(m_CommandList,
                &m_ShadowMap->GetView(), nullptr,
                *m_ShadowFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_ShadowDepthPass,
                context,
                "ShadowMap");
        }
        else
        {
            m_SunLight->shadowMap = nullptr;
        }

        std::vector<std::shared_ptr<LightProbe>> lightProbes;
        if (m_ui.EnableLightProbe)
        {
            for (auto probe : m_LightProbes)
            {
                if (probe->enabled)
                {
                    probe->diffuseScale = m_ui.LightProbeDiffuseScale;
                    probe->specularScale = m_ui.LightProbeSpecularScale;
                    lightProbes.push_back(probe);
                }
            }
        }

        m_RenderTargets->Clear(m_CommandList);

        if (exposureResetRequired)
            m_ToneMappingPass->ResetExposure(m_CommandList, 0.5f);

        ForwardShadingPass::Context forwardContext;

        if (!m_ui.UseDeferredShading || m_ui.EnableTranslucency)
        {
            m_ForwardPass->PrepareLights(forwardContext, m_CommandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, lightProbes);
        }

        if (m_ui.UseDeferredShading)
        {
            GBufferFillPass::Context gbufferContext;

            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->GBufferFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_GBufferPass,
                gbufferContext,
                "GBufferFill");

            nvrhi::ITexture* ambientOcclusionTarget = nullptr;
            if (m_ui.EnableSsao && m_SsaoPass)
            {
                m_SsaoPass->Render(m_CommandList, m_ui.SsaoParams, *m_View);
                ambientOcclusionTarget = m_RenderTargets->AmbientOcclusion;
            }

            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.SetGBuffer(*m_RenderTargets);
            deferredInputs.ambientOcclusion = m_ui.EnableSsao ? m_RenderTargets->AmbientOcclusion : nullptr;
            deferredInputs.ambientColorTop = m_AmbientTop;
            deferredInputs.ambientColorBottom = m_AmbientBottom;
            deferredInputs.lights = &m_Scene->GetSceneGraph()->GetLights();
            deferredInputs.lightProbes = m_ui.EnableLightProbe ? &m_LightProbes : nullptr;
            deferredInputs.output = m_RenderTargets->HdrColor;

            m_DeferredLightingPass->Render(m_CommandList, *m_View, deferredInputs);
        }
        else
        {
            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_ForwardPass,
                forwardContext,
                "ForwardOpaque");
        }

        {
            m_CommandList->clearTextureUInt(m_RenderTargets->MaterialIDs, nvrhi::AllSubresources, 0xffff);

            MaterialIDPass::Context materialIdContext;

            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->MaterialIDFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_MaterialIDPass,
                materialIdContext,
                "MaterialID");

            if (m_ui.EnableTranslucency)
            {
                RenderCompositeView(m_CommandList,
                    m_View.get(), m_ViewPrevious.get(),
                    *m_RenderTargets->MaterialIDFramebuffer,
                    m_Scene->GetSceneGraph()->GetRootNode(),
                    *m_TransparentDrawStrategy,
                    *m_MaterialIDPass,
                    materialIdContext,
                    "MaterialID - Translucent");
            }
        }

        if (m_ui.EnableProceduralSky)
            m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight, m_ui.SkyParams);

        if (m_ui.EnableTranslucency)
        {
            RenderCompositeView(m_CommandList,
                m_View.get(), m_ViewPrevious.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_TransparentDrawStrategy,
                *m_ForwardPass,
                forwardContext,
                "ForwardTransparent");
        }

        nvrhi::ITexture* finalHdrColor = m_RenderTargets->HdrColor;

        std::shared_ptr<FramebufferFactory> finalHdrFramebuffer = m_RenderTargets->HdrFramebuffer;

        if (m_RenderTargets->GetSampleCount() > 1)
        {
            auto subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
            m_CommandList->resolveTexture(m_RenderTargets->ResolvedColor, subresources, m_RenderTargets->HdrColor, subresources);
            finalHdrColor = m_RenderTargets->ResolvedColor;
            finalHdrFramebuffer = m_RenderTargets->ResolvedFramebuffer;
        }

        if (m_ui.EnableFXAA)
        {
            m_FXAAPass->Render(
                m_CommandList,
                finalHdrFramebuffer,
                *m_View,
                finalHdrColor
            );
        }

        if (m_ui.EnableBloom)
        {
            m_BloomPass->Render(m_CommandList, finalHdrFramebuffer, *m_View, finalHdrColor, m_ui.BloomSigma, m_ui.BloomAlpha);
        }

        m_PreviousViewsValid = false;

        auto toneMappingParams = m_ui.ToneMappingParams;
        if (exposureResetRequired)
        {
            toneMappingParams.eyeAdaptationSpeedUp = 0.f;
            toneMappingParams.eyeAdaptationSpeedDown = 0.f;
        }
        m_ToneMappingPass->SimpleRender(m_CommandList, toneMappingParams, *m_View, finalHdrColor);

        m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, m_BindingCache.get());

        if (m_ui.DisplayShadowMap)
        {
            for (int cascade = 0; cascade < 4; cascade++)
            {
                nvrhi::Viewport viewport = nvrhi::Viewport(
                    10.f + 266.f * cascade,
                    266.f * (1 + cascade),
                    windowViewport.maxY - 266.f,
                    windowViewport.maxY - 10.f, 0.f, 1.f
                );

                engine::BlitParameters blitParams;
                blitParams.targetFramebuffer = framebuffer;
                blitParams.targetViewport = viewport;
                blitParams.sourceTexture = m_ShadowMap->GetTexture();
                blitParams.sourceArraySlice = cascade;
                m_CommonPasses->BlitTexture(m_CommandList, blitParams, m_BindingCache.get());
            }
        }

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        std::swap(m_View, m_ViewPrevious);

        GetDeviceManager()->SetVsyncEnabled(m_ui.EnableVsync);
    }

    std::shared_ptr<Scene>& GetScene()
    {
        return m_Scene;
    }

    const std::shared_ptr<ShaderFactory>& GetShaderFactory() const
    {
        return m_ShaderFactory;
    }

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        m_Camera.KeyboardUpdate(key, scancode, action, mods);

        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            m_skipSplash = true;
        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        m_Camera.MousePosUpdate(xpos, ypos);
        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_Camera.MouseButtonUpdate(button, action, mods);

        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) // мыщка зажата
        {
            if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_DISABLED)
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                m_Camera.stopRotation = false;
            }
        }
        else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
        {
            if (glfwGetInputMode(window, GLFW_CURSOR) != GLFW_CURSOR_NORMAL)
            {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                m_Camera.stopRotation = true;
            }
        }

        return true;
    }

    std::vector<std::shared_ptr<LightProbe>>& GetLightProbes()
    {
        return m_LightProbes;
    }

    void CreateLightProbes(uint32_t numProbes)
    {
        nvrhi::DeviceHandle device = GetDeviceManager()->GetDevice();

        uint32_t diffuseMapSize = 256;
        uint32_t diffuseMapMipLevels = 1;
        uint32_t specularMapSize = 512;
        uint32_t specularMapMipLevels = 8;

        nvrhi::TextureDesc cubemapDesc;

        cubemapDesc.arraySize = 6 * numProbes;
        cubemapDesc.dimension = nvrhi::TextureDimension::TextureCubeArray;
        cubemapDesc.isRenderTarget = true;
        cubemapDesc.keepInitialState = true;

        cubemapDesc.width = diffuseMapSize;
        cubemapDesc.height = diffuseMapSize;
        cubemapDesc.mipLevels = diffuseMapMipLevels;
        cubemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
        cubemapDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        cubemapDesc.keepInitialState = true;

        m_LightProbeDiffuseTexture = device->createTexture(cubemapDesc);

        cubemapDesc.width = specularMapSize;
        cubemapDesc.height = specularMapSize;
        cubemapDesc.mipLevels = specularMapMipLevels;
        cubemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
        cubemapDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        cubemapDesc.keepInitialState = true;

        m_LightProbeSpecularTexture = device->createTexture(cubemapDesc);

        m_LightProbes.clear();

        for (uint32_t i = 0; i < numProbes; i++)
        {
            std::shared_ptr<LightProbe> probe = std::make_shared<LightProbe>();

            probe->name = std::to_string(i + 1);
            probe->diffuseMap = m_LightProbeDiffuseTexture;
            probe->specularMap = m_LightProbeSpecularTexture;
            probe->diffuseArrayIndex = i;
            probe->specularArrayIndex = i;
            probe->bounds = frustum::empty();
            probe->enabled = false;

            m_LightProbes.push_back(probe);
        }
    }

    void RenderLightProbe(LightProbe& probe)
    {
        nvrhi::DeviceHandle device = GetDeviceManager()->GetDevice();

        uint32_t environmentMapSize = 1024;
        uint32_t environmentMapMipLevels = 8;

        nvrhi::TextureDesc cubemapDesc;
        cubemapDesc.arraySize = 6;
        cubemapDesc.width = environmentMapSize;
        cubemapDesc.height = environmentMapSize;
        cubemapDesc.mipLevels = environmentMapMipLevels;
        cubemapDesc.dimension = nvrhi::TextureDimension::TextureCube;
        cubemapDesc.isRenderTarget = true;
        cubemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
        cubemapDesc.initialState = nvrhi::ResourceStates::RenderTarget;
        cubemapDesc.keepInitialState = true;
        cubemapDesc.clearValue = nvrhi::Color(0.f);
        cubemapDesc.useClearValue = true;

        nvrhi::TextureHandle colorTexture = device->createTexture(cubemapDesc);

        const nvrhi::Format depthFormats[] = {
            nvrhi::Format::D24S8,
            nvrhi::Format::D32,
            nvrhi::Format::D16,
            nvrhi::Format::D32S8 };

        const nvrhi::FormatSupport depthFeatures =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::DepthStencil |
            nvrhi::FormatSupport::ShaderLoad;

        cubemapDesc.mipLevels = 1;
        cubemapDesc.format = nvrhi::utils::ChooseFormat(GetDevice(), depthFeatures, depthFormats, std::size(depthFormats));
        cubemapDesc.isTypeless = true;
        cubemapDesc.initialState = nvrhi::ResourceStates::DepthWrite;

        nvrhi::TextureHandle depthTexture = device->createTexture(cubemapDesc);

        std::shared_ptr<FramebufferFactory> framebuffer = std::make_shared<FramebufferFactory>(device);
        framebuffer->RenderTargets = { colorTexture };
        framebuffer->DepthTarget = depthTexture;

        CubemapView view;
        view.SetArrayViewports(environmentMapSize, 0);
        const float nearPlane = 0.1f;
        const float cullDistance = 100.f;
        float3 probePosition = m_Camera.GetWorldToViewMatrix().m_translation;

        view.SetTransform(dm::translation(-probePosition), nearPlane, cullDistance);
        view.UpdateCache();

        std::shared_ptr<SkyPass> skyPass = std::make_shared<SkyPass>(device, m_ShaderFactory, m_CommonPasses, framebuffer, view);

        ForwardShadingPass::CreateParameters ForwardParams;
        ForwardParams.singlePassCubemap = GetDevice()->queryFeatureSupport(nvrhi::Feature::FastGeometryShader);
        std::shared_ptr<ForwardShadingPass> forwardPass = std::make_shared<ForwardShadingPass>(device, m_CommonPasses);
        forwardPass->Init(*m_ShaderFactory, ForwardParams);

        nvrhi::CommandListHandle commandList = device->createCommandList();
        commandList->open();
        commandList->clearTextureFloat(colorTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        const nvrhi::FormatInfo& depthFormatInfo = nvrhi::getFormatInfo(depthTexture->getDesc().format);
        commandList->clearDepthStencilTexture(depthTexture, nvrhi::AllSubresources, true, 0.f, depthFormatInfo.hasStencil, 0);

        box3 sceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();
        float zRange = length(sceneBounds.diagonal()) * 0.5f;
        m_ShadowMap->SetupForCubemapView(*m_SunLight, view.GetViewOrigin(), cullDistance, zRange, zRange, m_ui.CsmExponent);
        m_ShadowMap->Clear(commandList);

        DepthPass::Context shadowContext;

        RenderCompositeView(commandList,
            &m_ShadowMap->GetView(), nullptr,
            *m_ShadowFramebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_OpaqueDrawStrategy,
            *m_ShadowDepthPass,
            shadowContext,
            "ShadowMap");

        ForwardShadingPass::Context forwardContext;

        std::vector<std::shared_ptr<LightProbe>> lightProbes;
        forwardPass->PrepareLights(forwardContext, commandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, lightProbes);

        RenderCompositeView(commandList,
            &view, nullptr,
            *framebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_OpaqueDrawStrategy,
            *forwardPass,
            forwardContext,
            "ForwardOpaque");

        skyPass->Render(commandList, view, *m_SunLight, m_ui.SkyParams);

        RenderCompositeView(commandList,
            &view, nullptr,
            *framebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_TransparentDrawStrategy,
            *forwardPass,
            forwardContext,
            "ForwardTransparent");

        m_LightProbePass->GenerateCubemapMips(commandList, colorTexture, 0, 0, environmentMapMipLevels - 1);

        m_LightProbePass->RenderDiffuseMap(commandList, colorTexture, nvrhi::AllSubresources, probe.diffuseMap, probe.diffuseArrayIndex * 6, 0);

        uint32_t specularMapMipLevels = probe.specularMap->getDesc().mipLevels;
        for (uint32_t mipLevel = 0; mipLevel < specularMapMipLevels; mipLevel++)
        {
            float roughness = powf(float(mipLevel) / float(specularMapMipLevels - 1), 2.0f);
            m_LightProbePass->RenderSpecularMap(commandList, roughness, colorTexture, nvrhi::AllSubresources, probe.specularMap, probe.specularArrayIndex * 6, mipLevel);
        }

        m_LightProbePass->RenderEnvironmentBrdfTexture(commandList);

        commandList->close();
        device->executeCommandList(commandList);
        device->waitForIdle();
        device->runGarbageCollection();

        probe.environmentBrdf = m_LightProbePass->GetEnvironmentBrdfTexture();
        box3 bounds = box3(probePosition, probePosition).grow(10.f);
        probe.bounds = frustum::fromBox(bounds);
        probe.enabled = true;
    }
};


#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
#endif

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    {
        UIData uiData;

        std::unique_ptr<YupEngine> engine = std::make_unique<YupEngine>(deviceManager, uiData);
        std::unique_ptr<UIRenderer> gui = std::make_unique<UIRenderer>(
            deviceManager,
            uiData
        );

        gui->Init(engine->GetShaderFactory());

        deviceManager->AddRenderPassToBack(engine.get());
        deviceManager->AddRenderPassToBack(gui.get());
        deviceManager->RunMessageLoop();
    }

    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}
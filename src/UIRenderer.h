#pragma once

#include <donut/app/imgui_renderer.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/render/SkyPass.h>
#include <donut/render/SsaoPass.h>
#include <donut/engine/SceneGraph.h>
#include <donut/render/ToneMappingPasses.h>

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

struct UIData
{
    bool                                ShowUI = true;
    SkyParameters                       SkyParams;
    bool                                EnableSsao = true;
    bool                                EnableFXAA = true;
    SsaoParameters                      SsaoParams;
    ToneMappingParameters               ToneMappingParams;
    bool                                EnableVsync = true;
    bool                                EnableProceduralSky = true;
    bool                                EnableTranslucency = true;
    bool                                EnableShadows = true;
    bool                                ShaderReloadRequested = false;
    float                               AmbientIntensity = 1.0f;
    bool                                EnableLightProbe = true;
    float                               LightProbeDiffuseScale = 1.f;
    float                               LightProbeSpecularScale = 1.f;
    float                               CsmExponent = 4.f;
    bool                                DisplayShadowMap = false;
    bool                                EnableBloom = true;
    float                               BloomSigma = 32.f;
    float                               BloomAlpha = 0.05f;

    bool SceneLoadedStatus = false;
    std::vector<std::shared_ptr<engine::Light>> lights;
    std::vector<std::shared_ptr<engine::LightProbe>> LightProbes;

};

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<engine::Light> m_SelectedLight;
    UIData& m_ui;
    nvrhi::CommandListHandle m_CommandList;
    std::function<void(LightProbe& probe)> m_RenderCallback;

public:
    UIRenderer(DeviceManager* deviceManager, UIData& ui);

protected:
    virtual void buildUI(void) override;
};
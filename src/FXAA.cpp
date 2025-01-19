#include "FXAA.h"
#include <utility>

using namespace donut::render;
using namespace donut::engine;
using namespace donut::math;

FXAAPass::FXAAPass(nvrhi::IDevice* device,
    const std::shared_ptr<ShaderFactory>& shaderFactory,
    std::shared_ptr<CommonRenderPasses> commonPasses,
    std::shared_ptr<FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView)
    : m_CommonPasses(std::move(commonPasses))
    , m_FramebufferFactory(std::move(framebufferFactory))
    , m_Device(device)
    , m_BindingCache(device)
{
    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    nvrhi::Rect viewExtent = sampleView->GetViewExtent();
    int viewportWidth = viewExtent.maxX - viewExtent.minX;
    int viewportHeight = viewExtent.maxY - viewExtent.minY;

    // Load FXAA shader
    m_FXAAPixelShader = shaderFactory->CreateShader("FXAA.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

    // Define FXAA binding layout
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0)
    };
    m_FXAABindingLayout = device->createBindingLayout(layoutDesc);

    // intermediate texture for fxaa
    nvrhi::TextureDesc intermediateTextureDesc2;

    intermediateTextureDesc2.format = sampleFramebuffer->getFramebufferInfo().colorFormats[0];
    intermediateTextureDesc2.width = viewportWidth;
    intermediateTextureDesc2.height = viewportHeight;
    intermediateTextureDesc2.mipLevels = 1;
    intermediateTextureDesc2.isRenderTarget = true;

    intermediateTextureDesc2.debugName = "Fxaa Output Texture";
    intermediateTextureDesc2.initialState = nvrhi::ResourceStates::ShaderResource;
    intermediateTextureDesc2.keepInitialState = true;
    m_TextureOutput = m_Device->createTexture(intermediateTextureDesc2);
    m_FramebufferOutput = m_Device->createFramebuffer(nvrhi::FramebufferDesc()
        .addColorAttachment(m_TextureOutput));

    // Define graphics pipeline
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
    pipelineDesc.VS = sampleView->IsReverseDepth() ? m_CommonPasses->m_FullscreenVS : m_CommonPasses->m_FullscreenAtOneVS;
    pipelineDesc.PS = m_FXAAPixelShader;
    pipelineDesc.bindingLayouts = { m_FXAABindingLayout };
    pipelineDesc.renderState.rasterState.setCullNone();
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    pipelineDesc.renderState.depthStencilState.stencilEnable = false;
    m_FXAAPipeline = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);

}

void FXAAPass::Render(
    nvrhi::ICommandList* commandList,
    const std::shared_ptr<engine::FramebufferFactory>& framebufferFactory,
    const engine::ICompositeView& compositeView,
    nvrhi::ITexture* sourceDestTexture)
{
    commandList->beginMarker("FXAA");

    const donut::engine::IView* view = compositeView.GetChildView(donut::engine::ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* framebuffer = framebufferFactory->GetFramebuffer(*view);

    nvrhi::ViewportState viewportState = view->GetViewportState();

    // Define binding set

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler),
        nvrhi::BindingSetItem::Texture_SRV(0, sourceDestTexture) // Will be set dynamically
    };
    m_FXAABindingSet = m_Device->createBindingSet(bindingSetDesc, m_FXAABindingLayout);

    // Set up graphics state
    nvrhi::GraphicsState state;
    state.pipeline = m_FXAAPipeline;
    state.framebuffer = m_FramebufferOutput;
    state.viewport = viewportState;
    state.bindings = { m_FXAABindingSet };

    commandList->setGraphicsState(state);
    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args); // Fullscreen quad

    BlitParameters blitParams;
    blitParams.targetFramebuffer = framebuffer;
    blitParams.sourceTexture = m_TextureOutput;
    m_CommonPasses->BlitTexture(commandList, blitParams, &m_BindingCache); // blit texture to target

    commandList->endMarker();
}

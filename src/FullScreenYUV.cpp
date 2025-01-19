#include "FullScreenYUV.h"
#include <utility>

using namespace donut::render;
using namespace donut::engine;
using namespace donut::math;

FullScreenYUVPass::FullScreenYUVPass(nvrhi::IDevice* device,
    const std::shared_ptr<engine::ShaderFactory>& shaderFactory,
    std::shared_ptr<engine::CommonRenderPasses> commonPasses,
    std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
    const ICompositeView& compositeView)
    : m_CommonPasses(std::move(commonPasses))
    , m_FramebufferFactory(std::move(framebufferFactory))
    , m_Device(device)
    , m_BindingCache(device)
{
    const IView* sampleView = compositeView.GetChildView(ViewType::PLANAR, 0);
    nvrhi::IFramebuffer* sampleFramebuffer = m_FramebufferFactory->GetFramebuffer(*sampleView);

    // Load FullScreenYUV shader
    m_FullScreenYUVPixelShader = shaderFactory->CreateShader("FullScreenYUV.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);

    // Define FullScreenYUV binding layout
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Sampler(0),
        nvrhi::BindingLayoutItem::Texture_SRV(0)
    };
    m_BindingLayout = device->createBindingLayout(layoutDesc);

    // Define graphics pipeline
    nvrhi::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
    pipelineDesc.VS = sampleView->IsReverseDepth() ? m_CommonPasses->m_FullscreenVS : m_CommonPasses->m_FullscreenAtOneVS;
    pipelineDesc.PS = m_FullScreenYUVPixelShader;
    pipelineDesc.bindingLayouts = { m_BindingLayout };
    pipelineDesc.renderState.rasterState.setCullNone();
    pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
    pipelineDesc.renderState.depthStencilState.stencilEnable = false;
    m_Pipeline = device->createGraphicsPipeline(pipelineDesc, sampleFramebuffer);

}

void FullScreenYUVPass::Render(
    nvrhi::ICommandList* commandList,
    const std::shared_ptr<engine::FramebufferFactory>& framebufferFactory,
    const ICompositeView& compositeView,
    nvrhi::ITexture* sourceDestTexture
)
{
    commandList->beginMarker("FullScreenYUV");

    const donut::engine::IView* view = compositeView.GetChildView(donut::engine::ViewType::PLANAR, 0);
    nvrhi::ViewportState viewportState = view->GetViewportState();
    nvrhi::IFramebuffer* framebuffer = framebufferFactory->GetFramebuffer(*view);

    // Define binding set

    nvrhi::BindingSetDesc bindingSetDesc;
    bindingSetDesc.bindings = {
        nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler),
        nvrhi::BindingSetItem::Texture_SRV(0, sourceDestTexture) // YUV Will be set dynamically
    };
    m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);

    // Set up graphics state
    nvrhi::GraphicsState state;
    state.pipeline = m_Pipeline;
    state.framebuffer = framebuffer;
    state.viewport = viewportState;
    state.bindings = { m_BindingSet };

    commandList->setGraphicsState(state);

    nvrhi::DrawArguments args;
    args.instanceCount = 1;
    args.vertexCount = 4;
    commandList->draw(args); // Fullscreen quad

    commandList->endMarker();
}

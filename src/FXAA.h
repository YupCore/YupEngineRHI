#pragma once

#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/View.h>
#include <donut/core/math/math.h>
#include <nvrhi/nvrhi.h>
#include <memory>
#include <unordered_map>

namespace donut::engine
{
    class ShaderFactory;
    class CommonRenderPasses;
    class FramebufferFactory;
    class ICompositeView;
}

namespace donut::render
{
    class FXAAPass
    {
    public:
        FXAAPass(nvrhi::IDevice* device,
            const std::shared_ptr<engine::ShaderFactory>& shaderFactory,
            std::shared_ptr<engine::CommonRenderPasses> commonPasses,
            std::shared_ptr<engine::FramebufferFactory> framebufferFactory,
            const engine::ICompositeView& compositeView);

        void Render(
            nvrhi::ICommandList* commandList,
            const std::shared_ptr<engine::FramebufferFactory>& framebufferFactory,
            const engine::ICompositeView& compositeView,
            nvrhi::ITexture* sourceDestTexture);

    private:
        nvrhi::ShaderHandle m_FXAAPixelShader;
        nvrhi::BindingLayoutHandle m_FXAABindingLayout;
        nvrhi::BindingSetHandle m_FXAABindingSet;
        nvrhi::GraphicsPipelineHandle m_FXAAPipeline;

        nvrhi::TextureHandle m_TextureOutput;
        nvrhi::FramebufferHandle m_FramebufferOutput;

        std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;
        std::shared_ptr<engine::FramebufferFactory> m_FramebufferFactory;
        nvrhi::DeviceHandle m_Device;
        engine::BindingCache m_BindingCache;
    };
}

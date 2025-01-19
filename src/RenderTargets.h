#pragma once

#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/core/math/math.h>
#include <nvrhi/common/misc.h>


using namespace donut;
using namespace donut::math;
using namespace donut::engine;
using namespace donut::render;

class RenderTargets : public GBufferRenderTargets
{
public:
    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle MaterialIDs;
    nvrhi::TextureHandle ResolvedColor;
    nvrhi::TextureHandle AmbientOcclusion;

    nvrhi::HeapHandle Heap;

    std::shared_ptr<FramebufferFactory> ForwardFramebuffer;
    std::shared_ptr<FramebufferFactory> HdrFramebuffer;
    std::shared_ptr<FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<FramebufferFactory> ResolvedFramebuffer;
    std::shared_ptr<FramebufferFactory> MaterialIDFramebuffer;

    void Init(
        nvrhi::IDevice* device,
        dm::uint2 size,
        dm::uint sampleCount,
        bool enableMotionVectors,
        bool useReverseProjection) override;

    [[nodiscard]] bool IsUpdateRequired(uint2 size, uint sampleCount) const;

    void Clear(nvrhi::ICommandList* commandList) override;
};
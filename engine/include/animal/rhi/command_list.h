#pragma once

#include <animal/rhi/rhi.h>

#include <variant>
#include <vector>

namespace animal::rhi {

namespace cmd {

struct BeginRenderPass {
    RenderPassDesc desc;
};
struct EndRenderPass {};
struct SetPipeline {
    PipelineHandle pipeline;
};
struct SetViewport {
    Viewport viewport;
};
struct SetScissor {
    ScissorRect scissor;
};
struct SetVertexBuffer {
    uint32_t slot;
    BufferHandle buffer;
    uint64_t offset;
};
struct SetIndexBuffer {
    BufferHandle buffer;
    IndexType type;
    uint64_t offset;
};
struct SetBindGroup {
    uint32_t set;
    BindGroupHandle group;
};
struct Draw {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};
struct DrawIndexed {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};
struct BeginTimer {
    const char* name;
};
struct EndTimer {};

} // namespace cmd

using Command = std::variant<cmd::BeginRenderPass, cmd::EndRenderPass, cmd::SetPipeline, cmd::SetViewport,
                             cmd::SetScissor, cmd::SetVertexBuffer, cmd::SetIndexBuffer, cmd::SetBindGroup,
                             cmd::Draw, cmd::DrawIndexed, cmd::BeginTimer, cmd::EndTimer>;

// Backend-neutral command recording; translated by the backend at Device::submit().
// beginRenderPass() resets viewport and scissor to the full attachment; all other state
// (pipeline, buffers, bind groups) must be set again inside each pass.
class CommandList {
public:
    void reset();

    void beginRenderPass(const RenderPassDesc& desc);
    void endRenderPass();

    void setPipeline(PipelineHandle pipeline);
    void setViewport(const Viewport& viewport);
    void setScissor(const ScissorRect& scissor);
    void setVertexBuffer(uint32_t slot, BufferHandle buffer, uint64_t offset = 0);
    void setIndexBuffer(BufferHandle buffer, IndexType type, uint64_t offset = 0);
    void setBindGroup(uint32_t set, BindGroupHandle group);

    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                     int32_t vertexOffset = 0, uint32_t firstInstance = 0);

    // GPU time between the two calls (Device::gpuTimings). Outside render passes only; scopes may
    // nest and must be closed before submit. `name` must outlive the results (use a literal).
    void beginTimer(const char* name);
    void endTimer();
    uint32_t openTimers() const { return openTimers_; }

    const std::vector<Command>& commands() const { return commands_; }
    bool insideRenderPass() const { return insideRenderPass_; }

private:
    std::vector<Command> commands_;
    bool insideRenderPass_ = false;
    bool pipelineBound_ = false;
    uint32_t openTimers_ = 0;
};

} // namespace animal::rhi

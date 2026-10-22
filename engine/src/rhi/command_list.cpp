#include <animal/core/assert.h>
#include <animal/rhi/command_list.h>

namespace animal::rhi {

void CommandList::reset() {
    commands_.clear();
    insideRenderPass_ = false;
    pipelineBound_ = false;
    openTimers_ = 0;
}

void CommandList::beginTimer(const char* name) {
    ANIMAL_ASSERT(!insideRenderPass_ && name != nullptr);
    commands_.emplace_back(cmd::BeginTimer{name});
    ++openTimers_;
}

void CommandList::endTimer() {
    ANIMAL_ASSERT(!insideRenderPass_ && openTimers_ > 0);
    commands_.emplace_back(cmd::EndTimer{});
    --openTimers_;
}

void CommandList::beginRenderPass(const RenderPassDesc& desc) {
    ANIMAL_ASSERT(!insideRenderPass_);
    ANIMAL_ASSERT(desc.colorAttachmentCount > 0 && desc.colorAttachmentCount <= kMaxColorAttachments);
    commands_.emplace_back(cmd::BeginRenderPass{desc});
    insideRenderPass_ = true;
    pipelineBound_ = false;
}

void CommandList::endRenderPass() {
    ANIMAL_ASSERT(insideRenderPass_);
    commands_.emplace_back(cmd::EndRenderPass{});
    insideRenderPass_ = false;
}

void CommandList::setPipeline(PipelineHandle pipeline) {
    ANIMAL_ASSERT(insideRenderPass_ && pipeline.valid());
    commands_.emplace_back(cmd::SetPipeline{pipeline});
    pipelineBound_ = true;
}

void CommandList::setViewport(const Viewport& viewport) {
    ANIMAL_ASSERT(insideRenderPass_);
    commands_.emplace_back(cmd::SetViewport{viewport});
}

void CommandList::setScissor(const ScissorRect& scissor) {
    ANIMAL_ASSERT(insideRenderPass_);
    commands_.emplace_back(cmd::SetScissor{scissor});
}

void CommandList::setVertexBuffer(uint32_t slot, BufferHandle buffer, uint64_t offset) {
    ANIMAL_ASSERT(insideRenderPass_ && slot < kMaxVertexBuffers);
    commands_.emplace_back(cmd::SetVertexBuffer{slot, buffer, offset});
}

void CommandList::setIndexBuffer(BufferHandle buffer, IndexType type, uint64_t offset) {
    ANIMAL_ASSERT(insideRenderPass_);
    commands_.emplace_back(cmd::SetIndexBuffer{buffer, type, offset});
}

void CommandList::setBindGroup(uint32_t set, BindGroupHandle group) {
    ANIMAL_ASSERT(insideRenderPass_ && set < kMaxBindGroups);
    commands_.emplace_back(cmd::SetBindGroup{set, group});
}

void CommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    ANIMAL_ASSERT(insideRenderPass_ && pipelineBound_);
    commands_.emplace_back(cmd::Draw{vertexCount, instanceCount, firstVertex, firstInstance});
}

void CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                              uint32_t firstInstance) {
    ANIMAL_ASSERT(insideRenderPass_ && pipelineBound_);
    commands_.emplace_back(cmd::DrawIndexed{indexCount, instanceCount, firstIndex, vertexOffset, firstInstance});
}

} // namespace animal::rhi

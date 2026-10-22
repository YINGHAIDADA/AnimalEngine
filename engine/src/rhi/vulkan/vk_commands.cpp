#include "vk_device.h"

namespace animal::rhi::vulkan {

void VulkanDevice::submit(const CommandList& commands) {
    ANIMAL_ASSERT(frameActive_);
    ANIMAL_ASSERT(!commands.insideRenderPass() && commands.openTimers() == 0);
    std::vector<VkCommandBuffer> buffers;
    if (VkCommandBuffer upload = finishPendingUpload()) {
        buffers.push_back(upload);
    }
    VkCommandBuffer cmd = acquireCommandBuffer();
    Frame& frame = frames_[frameIndex_];
    if (caps_.gpuTimers && !frame.timerPoolReset) {
        vkCmdResetQueryPool(cmd, frame.timerPool, 0, TimerScopes::kMaxTimestamps);
        frame.timerPoolReset = true;
    }
    translate(commands, cmd);
    vkEndCommandBuffer(cmd);
    buffers.push_back(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = static_cast<uint32_t>(buffers.size());
    submitInfo.pCommandBuffers = buffers.data();
    check(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
}

// --- Command translation -------------------------------------------------------------

void VulkanDevice::translate(const CommandList& commandList, VkCommandBuffer cmd) {
    const std::vector<Command>& commands = commandList.commands();
    const Pipeline* pipeline = nullptr;
    std::array<const BindGroup*, kMaxBindGroups> boundGroups{};
    uint32_t dirtyGroups = 0;
    bool skipPass = false;

    auto bindDirtyGroups = [&] {
        for (uint32_t set = 0; set < kMaxBindGroups; ++set) {
            if ((dirtyGroups & (1u << set)) && boundGroups[set] && pipeline->setLayouts[set]) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, set, 1,
                                        &boundGroups[set]->set, 0, nullptr);
            }
        }
        dirtyGroups = 0;
    };

    for (size_t i = 0; i < commands.size(); ++i) {
        const Command& command = commands[i];
        if (const auto* begin = std::get_if<cmd::BeginRenderPass>(&command)) {
            const RenderPassDesc& desc = begin->desc;
            ImageBarriers barriers;

            // Everything sampled inside this pass must be readable before rendering starts.
            for (size_t j = i + 1; j < commands.size() && !std::holds_alternative<cmd::EndRenderPass>(commands[j]);
                 ++j) {
                if (const auto* setGroup = std::get_if<cmd::SetBindGroup>(&commands[j])) {
                    if (const BindGroup* group = bindGroups_.get(setGroup->group)) {
                        for (TextureHandle sampled : group->sampledTextures) {
                            if (Texture* texture = textures_.get(sampled)) {
                                barriers.add(*texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                            }
                        }
                    }
                }
            }

            std::array<VkRenderingAttachmentInfoKHR, kMaxColorAttachments> attachments{};
            Extent2D extent{UINT32_MAX, UINT32_MAX};
            skipPass = false;
            for (uint32_t a = 0; a < desc.colorAttachmentCount; ++a) {
                const ColorAttachment& color = desc.colorAttachments[a];
                Texture* texture = textures_.get(color.texture);
                if (!texture || !hasFlag(texture->usage, TextureUsage::RenderTarget)) {
                    log::error("Vulkan backend: render pass attachment is not a valid render target");
                    skipPass = true;
                    break;
                }
                barriers.add(*texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, color.loadOp != LoadOp::Load);
                attachments[a].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
                attachments[a].imageView = texture->view;
                attachments[a].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                attachments[a].loadOp = toVk(color.loadOp);
                attachments[a].storeOp =
                    color.storeOp == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachments[a].clearValue.color = {
                    {color.clearColor.r, color.clearColor.g, color.clearColor.b, color.clearColor.a}};
                extent.width = std::min(extent.width, texture->width);
                extent.height = std::min(extent.height, texture->height);
            }
            barriers.flush(cmd);
            if (skipPass) {
                continue;
            }

            VkRenderingInfoKHR rendering{};
            rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            rendering.renderArea = {{0, 0}, {extent.width, extent.height}};
            rendering.layerCount = 1;
            rendering.colorAttachmentCount = desc.colorAttachmentCount;
            rendering.pColorAttachments = attachments.data();
            vkCmdBeginRenderingKHR(cmd, &rendering);

            const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                                      0.0f, 1.0f};
            const VkRect2D scissor{{0, 0}, {extent.width, extent.height}};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            pipeline = nullptr;
            boundGroups = {};
            dirtyGroups = 0;
            continue;
        }

        if (std::holds_alternative<cmd::EndRenderPass>(command)) {
            if (!skipPass) {
                vkCmdEndRenderingKHR(cmd);
            }
            skipPass = false;
            continue;
        }
        const bool beginTimer = std::holds_alternative<cmd::BeginTimer>(command);
        if (beginTimer || std::holds_alternative<cmd::EndTimer>(command)) {
            if (caps_.gpuTimers) {
                Frame& frame = frames_[frameIndex_];
                const uint32_t slot =
                    beginTimer ? frame.timers.begin(std::get<cmd::BeginTimer>(command).name) : frame.timers.end();
                if (slot != TimerScopes::kNone) {
                    vkCmdWriteTimestamp2KHR(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.timerPool, slot);
                }
            }
            continue;
        }
        if (skipPass) {
            continue;
        }

        if (const auto* c = std::get_if<cmd::SetPipeline>(&command)) {
            pipeline = pipelines_.get(c->pipeline);
            if (pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
                for (uint32_t set = 0; set < kMaxBindGroups; ++set) {
                    if (boundGroups[set]) {
                        dirtyGroups |= 1u << set;
                    }
                }
            }
        } else if (const auto* viewport = std::get_if<cmd::SetViewport>(&command)) {
            const Viewport& v = viewport->viewport;
            const VkViewport vk{v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth};
            vkCmdSetViewport(cmd, 0, 1, &vk);
        } else if (const auto* scissor = std::get_if<cmd::SetScissor>(&command)) {
            const VkRect2D rect{{scissor->scissor.x, scissor->scissor.y},
                                {scissor->scissor.width, scissor->scissor.height}};
            vkCmdSetScissor(cmd, 0, 1, &rect);
        } else if (const auto* vb = std::get_if<cmd::SetVertexBuffer>(&command)) {
            if (const Buffer* buffer = buffers_.get(vb->buffer)) {
                const VkDeviceSize offset = vb->offset;
                vkCmdBindVertexBuffers(cmd, vb->slot, 1, &buffer->buffer, &offset);
            }
        } else if (const auto* ib = std::get_if<cmd::SetIndexBuffer>(&command)) {
            if (const Buffer* buffer = buffers_.get(ib->buffer)) {
                vkCmdBindIndexBuffer(cmd, buffer->buffer, ib->offset,
                                     ib->type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
            }
        } else if (const auto* group = std::get_if<cmd::SetBindGroup>(&command)) {
            const BindGroup* bindGroup = bindGroups_.get(group->group);
            if (!bindGroup || bindGroup->setIndex != group->set) {
                log::error("Vulkan backend: setBindGroup({}) with an invalid or mismatched bind group", group->set);
                continue;
            }
            boundGroups[group->set] = bindGroup;
            dirtyGroups |= 1u << group->set;
        } else if (const auto* draw = std::get_if<cmd::Draw>(&command)) {
            if (pipeline) {
                bindDirtyGroups();
                vkCmdDraw(cmd, draw->vertexCount, draw->instanceCount, draw->firstVertex, draw->firstInstance);
            }
        } else if (const auto* drawIndexed = std::get_if<cmd::DrawIndexed>(&command)) {
            if (pipeline) {
                bindDirtyGroups();
                vkCmdDrawIndexed(cmd, drawIndexed->indexCount, drawIndexed->instanceCount, drawIndexed->firstIndex,
                                 drawIndexed->vertexOffset, drawIndexed->firstInstance);
            }
        }
    }
}


} // namespace animal::rhi::vulkan

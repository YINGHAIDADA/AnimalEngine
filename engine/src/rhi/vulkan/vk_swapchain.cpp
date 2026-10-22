#include "vk_device.h"

namespace animal::rhi::vulkan {

bool VulkanDevice::createSwapchain(Extent2D desired) {
    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &surfaceCaps);

    VkExtent2D extent = surfaceCaps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(desired.width, surfaceCaps.minImageExtent.width, surfaceCaps.maxImageExtent.width);
        extent.height =
            std::clamp(desired.height, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &formatCount, formats.data());
    VkSurfaceFormatKHR surfaceFormat = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
            surfaceFormat = f;
            break;
        }
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &modeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_) {
        for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                presentMode = preferred;
                break;
            }
        }
    }

    uint32_t imageCount = std::max(surfaceCaps.minImageCount + 1, 2u);
    if (surfaceCaps.maxImageCount > 0) {
        imageCount = std::min(imageCount, surfaceCaps.maxImageCount);
    }
    if (!(surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        log::error("Vulkan: swapchain images cannot be a transfer destination");
        return false;
    }
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(surfaceCaps.supportedCompositeAlpha & compositeAlpha)) {
        for (uint32_t bit = 1; bit != 0; bit <<= 1) {
            if (surfaceCaps.supportedCompositeAlpha & bit) {
                compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(bit);
                break;
            }
        }
    }

    VkSwapchainKHR oldSwapchain = swapchain_;
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = surfaceCaps.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;
    if (!check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR")) {
        swapchain_ = oldSwapchain;
        return false;
    }
    if (oldSwapchain) {
        vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());

    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    renderFinished_.assign(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : renderFinished_) {
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore);
    }

    swapchainExtent_ = {extent.width, extent.height};
    return true;
}

bool VulkanDevice::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    const platform::PixelSize size = window_->pixelSize();
    if (!createSwapchain({size.width, size.height})) {
        return false;
    }
    lastWindowSize_ = size;
    swapchainDirty_ = false;

    Texture* texture = textures_.get(virtualSwapchain_);
    ANIMAL_ASSERT(texture != nullptr);
    destroyImageResources(*texture);
    texture->width = swapchainExtent_.width;
    texture->height = swapchainExtent_.height;
    texture->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    log::debug("Vulkan swapchain recreated: {}x{}", swapchainExtent_.width, swapchainExtent_.height);
    return createImageResources(*texture);
}

// --- Frames ---------------------------------------------------------------------

bool VulkanDevice::beginFrame() {
    ANIMAL_ASSERT(!frameActive_);
    Frame& frame = frames_[frameIndex_];
    vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    for (auto& fn : frame.deletions) {
        fn();
    }
    frame.deletions.clear();
    if (caps_.gpuTimers && !frame.timers.empty()) {
        const uint32_t count = frame.timers.used();
        std::vector<uint64_t> ticks(count);
        if (vkGetQueryPoolResults(device_, frame.timerPool, 0, count, count * sizeof(uint64_t), ticks.data(),
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            const uint64_t mask = timestampBits_ >= 64 ? ~uint64_t{0} : (uint64_t{1} << timestampBits_) - 1;
            for (uint64_t& t : ticks) {
                t &= mask;
            }
            gpuTimings_ = frame.timers.timings(ticks, timestampPeriod_);
        }
    }
    frame.timers.reset();
    frame.timerPoolReset = false;
    vkResetCommandPool(device_, frame.pool, 0);
    frame.commandBuffersUsed = 0;
    if (frame.stagingWanted > frame.staging.size) {
        // The last frame in this slot overflowed its staging buffer: grow it (the GPU is done with it).
        const VkDeviceSize size = std::bit_ceil(frame.stagingWanted);
        destroyHostBuffer(frame.staging);
        if (!createHostBuffer(size, frame.staging)) {
            createHostBuffer(kStagingBytesPerFrame, frame.staging);
        }
        log::info("Vulkan: frame staging buffer grown to {} KiB", frame.staging.size / 1024);
    }
    frame.stagingWanted = 0;
    frame.stagingOffset = 0;

    const platform::PixelSize size = window_->pixelSize();
    if (size.width == 0 || size.height == 0) {
        return false;
    }
    if (swapchainDirty_ || size.width != lastWindowSize_.width || size.height != lastWindowSize_.height) {
        if (!recreateSwapchain()) {
            return false;
        }
    }

    VkResult result =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!recreateSwapchain()) {
            return false;
        }
        result =
            vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE, &imageIndex_);
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        check(result, "vkAcquireNextImageKHR");
        return false;
    }
    swapchainDirty_ = result == VK_SUBOPTIMAL_KHR;
    vkResetFences(device_, 1, &frame.fence);
    frameActive_ = true;
    return true;
}

void VulkanDevice::endFrame() {
    ANIMAL_ASSERT(frameActive_);
    Frame& frame = frames_[frameIndex_];
    std::vector<VkCommandBuffer> buffers;
    if (VkCommandBuffer upload = finishPendingUpload()) {
        buffers.push_back(upload);
    }

    VkCommandBuffer cmd = acquireCommandBuffer();
    Texture* source = textures_.get(virtualSwapchain_);
    const VkImage target = swapchainImages_[imageIndex_];
    ImageBarriers barriers;
    barriers.add(*source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barriers.add(target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    barriers.flush(cmd);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(source->width), static_cast<int32_t>(source->height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(swapchainExtent_.width),
                          static_cast<int32_t>(swapchainExtent_.height), 1};
    vkCmdBlitImage(cmd, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    barriers.add(target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    barriers.flush(cmd);
    vkEndCommandBuffer(cmd);
    buffers.push_back(cmd);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = static_cast<uint32_t>(buffers.size());
    submitInfo.pCommandBuffers = buffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[imageIndex_];
    check(vkQueueSubmit(queue_, 1, &submitInfo, frame.fence), "vkQueueSubmit (present)");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[imageIndex_];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex_;
    const VkResult result = vkQueuePresentKHR(queue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        swapchainDirty_ = true;
    } else {
        check(result, "vkQueuePresentKHR");
    }

    frameActive_ = false;
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
}

void VulkanDevice::waitIdle() {
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
}


} // namespace animal::rhi::vulkan

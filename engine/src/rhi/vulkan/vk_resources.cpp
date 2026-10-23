#include "vk_device.h"

namespace animal::rhi::vulkan {

// --- Memory helpers --------------------------------------------------------------

bool VulkanDevice::allocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host, VkBuffer& buffer,
                                  VmaAllocation& allocation, void** mapped) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = host ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (host) {
        // Staging and readback: mapped for the buffer's lifetime, coherent so no flushes are needed.
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        alloc.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    VmaAllocationInfo result{};
    if (!check(vmaCreateBuffer(allocator_, &info, &alloc, &buffer, &allocation, &result), "vmaCreateBuffer")) {
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
        return false;
    }
    if (mapped) {
        *mapped = result.pMappedData;
    }
    return true;
}

bool VulkanDevice::createHostBuffer(VkDeviceSize size, HostBuffer& out) {
    out.size = size;
    return allocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                          out.buffer, out.allocation, &out.mapped);
}

void VulkanDevice::destroyHostBuffer(HostBuffer& buffer) {
    if (buffer.buffer) {
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

// A slot's deletions run once its fence has signalled, when the slot is reused. During a frame that
// is the current slot. Between frames frameIndex_ already names the next slot, whose fence belongs
// to an older frame, so the release waits for the most recently submitted frame instead.
void VulkanDevice::defer(std::function<void()> fn) {
    const auto count = static_cast<uint32_t>(frames_.size());
    const uint32_t slot = frameActive_ ? frameIndex_ : (frameIndex_ + count - 1) % count;
    frames_[slot].deletions.push_back(std::move(fn));
}

VkCommandBuffer VulkanDevice::acquireCommandBuffer() {
    Frame& frame = frames_[frameIndex_];
    if (frame.commandBuffersUsed == frame.commandBuffers.size()) {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = frame.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        frame.commandBuffers.push_back(cmd);
    }
    VkCommandBuffer cmd = frame.commandBuffers[frame.commandBuffersUsed++];
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    return cmd;
}

// --- Uploads ------------------------------------------------------------------

VkCommandBuffer VulkanDevice::uploadCommands() {
    if (!pendingUpload_) {
        pendingUpload_ = acquireCommandBuffer();
        bufferBarrier(pendingUpload_, true);
    }
    return pendingUpload_;
}

VkCommandBuffer VulkanDevice::finishPendingUpload() {
    VkCommandBuffer cmd = pendingUpload_;
    if (cmd) {
        bufferBarrier(cmd, false);
        vkEndCommandBuffer(cmd);
        pendingUpload_ = VK_NULL_HANDLE;
    }
    return cmd;
}

void VulkanDevice::stage(const void* data, VkDeviceSize size, VkBuffer& buffer, VkDeviceSize& offset) {
    Frame& frame = frames_[frameIndex_];
    const VkDeviceSize aligned = (frame.stagingOffset + 15) & ~VkDeviceSize{15};
    frame.stagingWanted = std::max(frame.stagingWanted, aligned + size);
    if (aligned + size <= frame.staging.size) {
        std::memcpy(static_cast<uint8_t*>(frame.staging.mapped) + aligned, data, size);
        frame.stagingOffset = aligned + size;
        buffer = frame.staging.buffer;
        offset = aligned;
        return;
    }
    // Frame staging exhausted: a dedicated buffer, released once this frame retires (the slot's
    // staging buffer grows to fit the next time it begins a frame).
    HostBuffer overflow;
    createHostBuffer(size, overflow);
    std::memcpy(overflow.mapped, data, size);
    buffer = overflow.buffer;
    offset = 0;
    defer([this, overflow]() mutable { destroyHostBuffer(overflow); });
}

void VulkanDevice::immediate(const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = immediatePool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &alloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    bufferBarrier(cmd, true);
    record(cmd);
    bufferBarrier(cmd, false);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (immediate)");
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, immediatePool_, 1, &cmd);
}

void VulkanDevice::recordTextureCopy(VkCommandBuffer cmd, Texture& texture, VkBuffer source,
                                     VkDeviceSize sourceOffset, const TextureRegion& region, uint32_t rowLength) {
    ImageBarriers barriers;
    const bool whole = region.x == 0 && region.y == 0 && region.width == texture.width &&
                       region.height == texture.height;
    barriers.use(texture, ImageUse::CopyDst, whole);
    barriers.flush(cmd);

    VkBufferImageCopy copy{};
    copy.bufferOffset = sourceOffset;
    copy.bufferRowLength = rowLength;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageOffset = {static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), 0};
    copy.imageExtent = {region.width, region.height, 1};
    vkCmdCopyBufferToImage(cmd, source, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if (hasFlag(texture.usage, TextureUsage::Sampled)) {
        barriers.use(texture, ImageUse::Sampled);
        barriers.flush(cmd);
    }
}

void VulkanDevice::writeBuffer(BufferHandle handle, uint64_t offset, const void* data, uint64_t size) {
    const Buffer* buffer = buffers_.get(handle);
    if (!buffer || offset + size > buffer->size) {
        log::error("writeBuffer: invalid buffer or range [{}, {})", offset, offset + size);
        return;
    }
    if (size == 0) {
        return;
    }
    const VkBuffer target = buffer->buffer;
    if (frameActive_) {
        VkBuffer source = VK_NULL_HANDLE;
        VkDeviceSize sourceOffset = 0;
        stage(data, size, source, sourceOffset);
        const VkBufferCopy copy{sourceOffset, offset, size};
        vkCmdCopyBuffer(uploadCommands(), source, target, 1, &copy);
        return;
    }
    HostBuffer temp;
    if (!createHostBuffer(size, temp)) {
        return;
    }
    std::memcpy(temp.mapped, data, size);
    immediate([&](VkCommandBuffer cmd) {
        const VkBufferCopy copy{0, offset, size};
        vkCmdCopyBuffer(cmd, temp.buffer, target, 1, &copy);
    });
    destroyHostBuffer(temp);
}

void VulkanDevice::writeTexture(TextureHandle handle, const TextureRegion& region, const void* pixels,
                                uint32_t bytesPerRow) {
    Texture* texture = textures_.get(handle);
    if (!texture) {
        log::error("writeTexture: invalid texture");
        return;
    }
    const uint32_t bpp = bytesPerPixel(texture->format);
    if (region.x + region.width > texture->width || region.y + region.height > texture->height ||
        bytesPerRow % bpp != 0 || bytesPerRow / bpp < region.width) {
        log::error("writeTexture: region or row pitch out of range");
        return;
    }
    const VkDeviceSize size = VkDeviceSize{bytesPerRow} * (region.height - 1) + VkDeviceSize{region.width} * bpp;
    const uint32_t rowLength = bytesPerRow / bpp;

    if (frameActive_) {
        VkBuffer source = VK_NULL_HANDLE;
        VkDeviceSize sourceOffset = 0;
        stage(pixels, size, source, sourceOffset);
        recordTextureCopy(uploadCommands(), *texture, source, sourceOffset, region, rowLength);
        return;
    }
    HostBuffer temp;
    if (!createHostBuffer(size, temp)) {
        return;
    }
    std::memcpy(temp.mapped, pixels, size);
    immediate([&](VkCommandBuffer cmd) { recordTextureCopy(cmd, *texture, temp.buffer, 0, region, rowLength); });
    destroyHostBuffer(temp);
}

std::vector<uint8_t> VulkanDevice::readTexture(TextureHandle handle) {
    Texture* texture = textures_.get(handle);
    if (!texture) {
        log::error("readTexture: invalid texture");
        return {};
    }
    // Recorded-but-unsubmitted uploads already changed the tracked layouts; submit them first.
    if (VkCommandBuffer upload = finishPendingUpload()) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &upload;
        check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit (upload)");
    }
    vkQueueWaitIdle(queue_);

    const VkDeviceSize size = VkDeviceSize{texture->width} * texture->height * bytesPerPixel(texture->format);
    HostBuffer temp;
    if (!createHostBuffer(size, temp)) {
        return {};
    }
    immediate([&](VkCommandBuffer cmd) {
        ImageBarriers barriers;
        barriers.use(*texture, ImageUse::CopySrc); // the next use transitions it back
        barriers.flush(cmd);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {texture->width, texture->height, 1};
        vkCmdCopyImageToBuffer(cmd, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, temp.buffer, 1, &copy);
    });
    std::vector<uint8_t> pixels(size);
    std::memcpy(pixels.data(), temp.mapped, size);
    destroyHostBuffer(temp);
    return pixels;
}

// --- Resource creation -------------------------------------------------------------

BufferHandle VulkanDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
    if (desc.size == 0) {
        log::error("createBuffer '{}': size must be non-zero", nameOr(desc.debugName, "unnamed"));
        return {};
    }
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(desc.usage, BufferUsage::Vertex)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (hasFlag(desc.usage, BufferUsage::Index)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (hasFlag(desc.usage, BufferUsage::Uniform)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    Buffer buffer;
    buffer.size = desc.size;
    buffer.usage = desc.usage;
    if (!allocateBuffer(desc.size, usage, false, buffer.buffer, buffer.allocation)) {
        return {};
    }
    const BufferHandle handle = buffers_.insert(buffer);
    if (initialData) {
        writeBuffer(handle, 0, initialData, desc.size);
    }
    return handle;
}

bool VulkanDevice::createImageResources(Texture& texture) {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(texture.usage, TextureUsage::Sampled)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (hasFlag(texture.usage, TextureUsage::RenderTarget)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = toVk(texture.format);
    info.extent = {texture.width, texture.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!check(vmaCreateImage(allocator_, &info, &alloc, &texture.image, &texture.allocation, nullptr),
               "vmaCreateImage")) {
        texture.image = VK_NULL_HANDLE;
        return false;
    }

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = texture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!check(vkCreateImageView(device_, &view, nullptr, &texture.view), "vkCreateImageView")) {
        destroyImageResources(texture);
        return false;
    }
    texture.use = ImageUse::None;
    return true;
}

void VulkanDevice::destroyImageResources(Texture& texture) {
    if (texture.view) {
        vkDestroyImageView(device_, texture.view, nullptr);
    }
    if (texture.image) {
        vmaDestroyImage(allocator_, texture.image, texture.allocation);
    }
    texture.view = VK_NULL_HANDLE;
    texture.image = VK_NULL_HANDLE;
    texture.allocation = VK_NULL_HANDLE;
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    if (desc.width == 0 || desc.height == 0 || desc.width > caps_.maxTextureSize ||
        desc.height > caps_.maxTextureSize) {
        log::error("createTexture '{}': invalid size {}x{}", name, desc.width, desc.height);
        return {};
    }
    if (toVk(desc.format) == VK_FORMAT_UNDEFINED) {
        log::error("createTexture '{}': unsupported format", name);
        return {};
    }
    Texture texture;
    texture.width = desc.width;
    texture.height = desc.height;
    texture.format = desc.format;
    texture.usage = desc.usage;
    if (!createImageResources(texture)) {
        log::error("createTexture '{}' failed", name);
        return {};
    }
    return textures_.insert(texture);
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc& desc) {
    auto filter = [](Filter f) { return f == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; };
    auto address = [](AddressMode m) {
        return m == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    };
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = filter(desc.magFilter);
    info.minFilter = filter(desc.minFilter);
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address(desc.addressU);
    info.addressModeV = address(desc.addressV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    Sampler sampler;
    if (!check(vkCreateSampler(device_, &info, nullptr, &sampler.sampler), "vkCreateSampler")) {
        return {};
    }
    return samplers_.insert(sampler);
}

// --- Destruction (deferred until the GPU is done with the current frame slot) ---------------

void VulkanDevice::destroy(BufferHandle handle) {
    if (auto buffer = buffers_.take(handle)) {
        defer([this, b = *buffer] { vmaDestroyBuffer(allocator_, b.buffer, b.allocation); });
    }
}

void VulkanDevice::destroy(TextureHandle handle) {
    if (handle == virtualSwapchain_) {
        log::error("destroy: the swapchain texture is owned by the device");
        return;
    }
    if (auto texture = textures_.take(handle)) {
        defer([this, t = *texture]() mutable { destroyImageResources(t); });
    }
}

void VulkanDevice::destroy(SamplerHandle handle) {
    if (auto sampler = samplers_.take(handle)) {
        defer([this, s = sampler->sampler] { vkDestroySampler(device_, s, nullptr); });
    }
}

void VulkanDevice::destroy(ShaderHandle handle) {
    if (auto shader = shaders_.take(handle)) {
        defer([this, m = shader->module] { vkDestroyShaderModule(device_, m, nullptr); });
    }
}

void VulkanDevice::destroy(PipelineHandle handle) {
    if (auto pipeline = pipelines_.take(handle)) {
        defer([this, p = pipeline->pipeline, l = pipeline->layout] {
            vkDestroyPipeline(device_, p, nullptr);
            vkDestroyPipelineLayout(device_, l, nullptr);
        });
    }
}

void VulkanDevice::destroy(BindGroupHandle handle) {
    if (auto group = bindGroups_.take(handle)) {
        defer([this, g = *group] { vkFreeDescriptorSets(device_, g.pool, 1, &g.set); });
    }
}


} // namespace animal::rhi::vulkan

#include "vk_device.h"

namespace animal::rhi::vulkan {

ShaderHandle VulkanDevice::createShader(const ShaderDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    if (!desc.package || desc.package->spirv.empty()) {
        log::error("createShader '{}': package has no SPIR-V code", name);
        return {};
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = desc.package->spirv.size() * sizeof(uint32_t);
    info.pCode = desc.package->spirv.data();
    Shader shader;
    if (!check(vkCreateShaderModule(device_, &info, nullptr, &shader.module), "vkCreateShaderModule")) {
        return {};
    }
    shader.stage = desc.package->stage;
    shader.bindings = desc.package->bindings;
    shader.name = name;
    return shaders_.insert(std::move(shader));
}

VkDescriptorSetLayout VulkanDevice::setLayout(const std::vector<ShaderBinding>& bindings, uint32_t set) {
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    std::string signature;
    for (const ShaderBinding& b : bindings) {
        if (b.set != set) {
            continue;
        }
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = b.binding;
        binding.descriptorType = toVk(b.kind);
        binding.descriptorCount = 1;
        // Same stage flags everywhere so identical sets are compatible across pipelines.
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        vkBindings.push_back(binding);
    }
    std::sort(vkBindings.begin(), vkBindings.end(),
              [](const auto& a, const auto& b) { return a.binding < b.binding; });
    for (const auto& b : vkBindings) {
        signature += std::format("{}:{};", b.binding, static_cast<int>(b.descriptorType));
    }

    if (auto it = setLayoutCache_.find(signature); it != setLayoutCache_.end()) {
        return it->second;
    }
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vkBindings.size());
    info.pBindings = vkBindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    if (!check(vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout), "vkCreateDescriptorSetLayout")) {
        return VK_NULL_HANDLE;
    }
    setLayoutCache_.emplace(signature, layout);
    return layout;
}

PipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    const Shader* vs = shaders_.get(desc.vertexShader);
    const Shader* fs = shaders_.get(desc.fragmentShader);
    if (!vs || !fs || vs->stage != ShaderStage::Vertex || fs->stage != ShaderStage::Fragment) {
        log::error("createGraphicsPipeline '{}': needs a valid vertex and fragment shader", name);
        return {};
    }
    if (desc.colorTargets.empty() || desc.colorTargets.size() > kMaxColorAttachments) {
        log::error("createGraphicsPipeline '{}': needs 1..{} color targets", name, kMaxColorAttachments);
        return {};
    }

    std::vector<ShaderBinding> bindings = vs->bindings;
    for (const ShaderBinding& b : fs->bindings) {
        auto existing = std::find_if(bindings.begin(), bindings.end(), [&b](const ShaderBinding& e) {
            return e.set == b.set && e.binding == b.binding;
        });
        if (existing == bindings.end()) {
            bindings.push_back(b);
        } else if (existing->kind != b.kind) {
            log::error("createGraphicsPipeline '{}': set {} binding {} has different types per stage", name, b.set,
                       b.binding);
            return {};
        }
    }

    Pipeline pipeline;
    pipeline.bindings = bindings;
    uint32_t setCount = 0;
    for (const ShaderBinding& b : bindings) {
        setCount = std::max(setCount, b.set + 1);
    }
    for (uint32_t s = 0; s < setCount; ++s) {
        pipeline.setLayouts[s] = setLayout(bindings, s);
        if (!pipeline.setLayouts[s]) {
            return {};
        }
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = setCount;
    layoutInfo.pSetLayouts = pipeline.setLayouts.data();
    if (!check(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipeline.layout), "vkCreatePipelineLayout")) {
        return {};
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs->module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs->module;
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> vertexBindings;
    for (uint32_t i = 0; i < desc.vertexBuffers.size(); ++i) {
        vertexBindings.push_back({i, desc.vertexBuffers[i].stride,
                                  desc.vertexBuffers[i].stepMode == VertexStepMode::Instance
                                      ? VK_VERTEX_INPUT_RATE_INSTANCE
                                      : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    for (const VertexAttribute& a : desc.vertexAttributes) {
        vertexAttributes.push_back({a.location, a.bufferSlot, toVk(a.format), a.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
    vertexInput.pVertexBindingDescriptions = vertexBindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVk(desc.topology);

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = toVk(desc.cullMode);
    raster.frontFace = desc.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                      : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    std::vector<VkFormat> colorFormats;
    for (const ColorTargetDesc& target : desc.colorTargets) {
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = target.blend.enabled ? VK_TRUE : VK_FALSE;
        blend.srcColorBlendFactor = toVk(target.blend.srcColor);
        blend.dstColorBlendFactor = toVk(target.blend.dstColor);
        blend.colorBlendOp = toVk(target.blend.colorOp);
        blend.srcAlphaBlendFactor = toVk(target.blend.srcAlpha);
        blend.dstAlphaBlendFactor = toVk(target.blend.dstAlpha);
        blend.alphaBlendOp = toVk(target.blend.alphaOp);
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        blendAttachments.push_back(blend);
        colorFormats.push_back(toVk(target.format));
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlend.pAttachments = blendAttachments.data();

    const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfoKHR rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();

    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = pipeline.layout;
    const uint64_t begin = platform::nowNanoseconds();
    const VkResult created = vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &info, nullptr, &pipeline.pipeline);
    pipelineMilliseconds_ += static_cast<double>(platform::nowNanoseconds() - begin) * 1e-6;
    if (!check(created, "vkCreateGraphicsPipelines")) {
        vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
        return {};
    }
    ++pipelinesCreated_;
    return pipelines_.insert(std::move(pipeline));
}

BindGroupHandle VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
    const char* name = nameOr(desc.debugName, "unnamed");
    const Pipeline* pipeline = pipelines_.get(desc.pipeline);
    if (!pipeline || desc.set >= kMaxBindGroups || !pipeline->setLayouts[desc.set]) {
        log::error("createBindGroup '{}': pipeline has no layout for set {}", name, desc.set);
        return {};
    }

    BindGroup group;
    group.setIndex = desc.set;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    std::vector<VkWriteDescriptorSet> writes;
    uint32_t provided = 0;

    for (const BindGroupEntry& entry : desc.entries) {
        auto layout = std::find_if(pipeline->bindings.begin(), pipeline->bindings.end(),
                                   [&](const ShaderBinding& b) { return b.set == desc.set && b.binding == entry.binding; });
        if (layout == pipeline->bindings.end()) {
            log::error("createBindGroup '{}': pipeline has no resource at set {} binding {}", name, desc.set,
                       entry.binding);
            return {};
        }
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = toVk(layout->kind);
        if (layout->kind == BindingKind::UniformBuffer) {
            const Buffer* buffer = buffers_.get(entry.buffer);
            if (!buffer || !hasFlag(buffer->usage, BufferUsage::Uniform) ||
                entry.offset % caps_.uniformBufferAlignment != 0 || entry.offset + entry.size > buffer->size) {
                log::error("createBindGroup '{}': binding {} needs an aligned uniform buffer range", name,
                           entry.binding);
                return {};
            }
            bufferInfos.push_back({buffer->buffer, entry.offset, entry.size ? entry.size : VK_WHOLE_SIZE});
            write.pBufferInfo = &bufferInfos.back();
        } else {
            const Texture* texture = textures_.get(entry.texture);
            const Sampler* sampler = samplers_.get(entry.sampler);
            if (!texture || !hasFlag(texture->usage, TextureUsage::Sampled) || !sampler) {
                log::error("createBindGroup '{}': binding {} needs a sampled texture and a sampler", name,
                           entry.binding);
                return {};
            }
            imageInfos.push_back({sampler->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pImageInfo = &imageInfos.back();
            group.sampledTextures.push_back(entry.texture);
        }
        writes.push_back(write);
        provided |= 1u << entry.binding;
    }
    for (const ShaderBinding& b : pipeline->bindings) {
        if (b.set == desc.set && !(provided & (1u << b.binding))) {
            log::error("createBindGroup '{}': missing resource for set {} binding {}", name, b.set, b.binding);
            return {};
        }
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descriptorPools_.back();
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &pipeline->setLayouts[desc.set];
    VkResult result = vkAllocateDescriptorSets(device_, &alloc, &group.set);
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        if (!createDescriptorPool()) { // the current pool is full: continue in a new one
            return {};
        }
        alloc.descriptorPool = descriptorPools_.back();
        result = vkAllocateDescriptorSets(device_, &alloc, &group.set);
    }
    if (!check(result, "vkAllocateDescriptorSets")) {
        return {};
    }
    group.pool = alloc.descriptorPool;
    for (VkWriteDescriptorSet& write : writes) {
        write.dstSet = group.set;
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return bindGroups_.insert(std::move(group));
}


// --- Pipeline cache (ADR-0024) -------------------------------------------------------------

namespace {

// VkPipelineCacheHeaderVersionOne, written by the driver at the start of the cache data.
struct CacheHeader {
    uint32_t size;
    uint32_t version;
    uint32_t vendorId;
    uint32_t deviceId;
    uint8_t uuid[VK_UUID_SIZE];
};

} // namespace

void VulkanDevice::createPipelineCache() {
    if (const std::string dir = platform::userDataDirectory("AnimalEngine", "Engine"); !dir.empty()) {
        pipelineCachePath_ = dir + "vulkan_pipelines.bin";
    }
    std::vector<std::byte> data;
    if (!pipelineCachePath_.empty() && fileExists(pipelineCachePath_)) {
        if (auto bytes = readFile(pipelineCachePath_)) {
            CacheHeader header{};
            if (bytes->size() >= sizeof(header)) {
                std::memcpy(&header, bytes->data(), sizeof(header));
            }
            // A cache from another GPU or driver version is ignored rather than handed to the driver.
            if (header.version == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && header.vendorId == vendorId_ &&
                header.deviceId == deviceId_ && std::memcmp(header.uuid, pipelineCacheUuid_.data(), VK_UUID_SIZE) == 0) {
                data = std::move(*bytes);
            } else {
                log::info("Vulkan: pipeline cache belongs to another device or driver; starting empty");
            }
        }
    }
    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_) != VK_SUCCESS && !data.empty()) {
        data.clear();
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        check(vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_), "vkCreatePipelineCache");
    }
    pipelineCacheLoaded_ = data.size();
}

void VulkanDevice::savePipelineCache() {
    size_t size = 0;
    if (pipelineCachePath_.empty() || vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS ||
        size == 0) {
        return;
    }
    std::vector<std::byte> data(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) == VK_SUCCESS &&
        !writeFileAtomic(pipelineCachePath_, data.data(), size)) {
        log::warn("Vulkan: could not write the pipeline cache {}", pipelineCachePath_);
    }
}

} // namespace animal::rhi::vulkan

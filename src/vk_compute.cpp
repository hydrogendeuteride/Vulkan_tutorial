#include <vk_compute.h>
#include <vk_engine.h>
#include <vk_pipelines.h>
#include <vk_initializers.h>
#include <iostream>

ComputeBinding ComputeBinding::uniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset)
{
    ComputeBinding result;
    result.binding = binding;
    result.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    result.buffer.buffer = buffer;
    result.buffer.offset = offset;
    result.buffer.size = size;
    return result;
}

ComputeBinding ComputeBinding::storageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset)
{
    ComputeBinding result;
    result.binding = binding;
    result.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    result.buffer.buffer = buffer;
    result.buffer.offset = offset;
    result.buffer.size = size;
    return result;
}

ComputeBinding ComputeBinding::sampledImage(uint32_t binding, VkImageView imageView, VkSampler sampler,
                                            VkImageLayout layout)
{
    ComputeBinding result;
    result.binding = binding;
    result.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    result.image.imageView = imageView;
    result.image.sampler = sampler;
    result.image.layout = layout;
    return result;
}

ComputeBinding ComputeBinding::makeStorageImage(uint32_t binding, VkImageView imageView, VkImageLayout layout)
{
    ComputeBinding result;
    result.binding = binding;
    result.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    result.storageImage.imageView = imageView;
    result.storageImage.layout = layout;
    return result;
}

ComputePipeline::~ComputePipeline()
{
    cleanup();
}

ComputePipeline::ComputePipeline(ComputePipeline &&other) noexcept
    : device(other.device)
      , pipeline(other.pipeline)
      , layout(other.layout)
      , descriptorLayout(other.descriptorLayout)
{
    other.device = VK_NULL_HANDLE;
    other.pipeline = VK_NULL_HANDLE;
    other.layout = VK_NULL_HANDLE;
    other.descriptorLayout = VK_NULL_HANDLE;
}

ComputePipeline &ComputePipeline::operator=(ComputePipeline &&other) noexcept
{
    if (this != &other)
    {
        cleanup();

        device = other.device;
        pipeline = other.pipeline;
        layout = other.layout;
        descriptorLayout = other.descriptorLayout;

        other.device = VK_NULL_HANDLE;
        other.pipeline = VK_NULL_HANDLE;
        other.layout = VK_NULL_HANDLE;
        other.descriptorLayout = VK_NULL_HANDLE;
    }
    return *this;
}

void ComputePipeline::cleanup()
{
    if (device != VK_NULL_HANDLE)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, layout, nullptr);
        }
        if (descriptorLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
        }
    }

    device = VK_NULL_HANDLE;
    pipeline = VK_NULL_HANDLE;
    layout = VK_NULL_HANDLE;
    descriptorLayout = VK_NULL_HANDLE;
}

ComputeManager::~ComputeManager()
{
    cleanup();
}

void ComputeManager::init(VulkanEngine *engine)
{
    this->engine = engine;

    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> poolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}
    };

    descriptorAllocator.init(engine->_device, 100, poolSizes);

    // registerBuiltinPipelines();
}

void ComputeManager::cleanup()
{
    pipelines.clear();

    if (engine)
    {
        descriptorAllocator.destroy_pools(engine->_device);
    }

    engine = nullptr;
}

bool ComputeManager::registerPipeline(const std::string &name, const ComputePipelineCreateInfo &createInfo)
{
    if (pipelines.find(name) != pipelines.end())
    {
        std::cerr << "Pipeline '" << name << "' already exists!" << std::endl;
        return false;
    }

    return createPipeline(name, createInfo);
}

void ComputeManager::unregisterPipeline(const std::string &name)
{
    pipelines.erase(name);
}

bool ComputeManager::hasPipeline(const std::string &name) const
{
    return pipelines.find(name) != pipelines.end();
}

void ComputeManager::dispatch(VkCommandBuffer cmd, const std::string &pipelineName,
                              const ComputeDispatchInfo &dispatchInfo)
{
    auto it = pipelines.find(pipelineName);
    if (it == pipelines.end())
    {
        std::cerr << "Pipeline '" << pipelineName << "' not found!" << std::endl;
        return;
    }

    const ComputePipeline &pipeline = it->second;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getPipeline());

    if (!dispatchInfo.bindings.empty())
    {
        VkDescriptorSet descriptorSet = allocateDescriptorSet(pipeline, dispatchInfo.bindings);
        updateDescriptorSet(descriptorSet, dispatchInfo.bindings);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getLayout(),
                                0, 1, &descriptorSet, 0, nullptr);
    }

    if (dispatchInfo.pushConstants && dispatchInfo.pushConstantSize > 0)
    {
        vkCmdPushConstants(cmd, pipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, dispatchInfo.pushConstantSize, dispatchInfo.pushConstants);
    }

    insertBarriers(cmd, dispatchInfo);

    vkCmdDispatch(cmd, dispatchInfo.groupCountX, dispatchInfo.groupCountY, dispatchInfo.groupCountZ);
}

void ComputeManager::dispatchImmediate(const std::string &pipelineName, const ComputeDispatchInfo &dispatchInfo)
{
    engine->immediate_submit([this, pipelineName, dispatchInfo](VkCommandBuffer cmd) {
        dispatch(cmd, pipelineName, dispatchInfo);
    });
}

uint32_t ComputeManager::calculateGroupCount(uint32_t workItems, uint32_t localSize)
{
    return (workItems + localSize - 1) / localSize;
}

ComputeDispatchInfo ComputeManager::createDispatch2D(uint32_t width, uint32_t height, uint32_t localSizeX,
                                                     uint32_t localSizeY)
{
    ComputeDispatchInfo info;
    info.groupCountX = calculateGroupCount(width, localSizeX);
    info.groupCountY = calculateGroupCount(height, localSizeY);
    info.groupCountZ = 1;
    return info;
}

ComputeDispatchInfo ComputeManager::createDispatch3D(uint32_t width, uint32_t height, uint32_t depth,
                                                     uint32_t localSizeX, uint32_t localSizeY, uint32_t localSizeZ)
{
    ComputeDispatchInfo info;
    info.groupCountX = calculateGroupCount(width, localSizeX);
    info.groupCountY = calculateGroupCount(height, localSizeY);
    info.groupCountZ = calculateGroupCount(depth, localSizeZ);
    return info;
}

void ComputeManager::clearImage(VkCommandBuffer cmd, VkImageView imageView, const glm::vec4 &clearColor)
{
    if (!hasPipeline("clear_image"))
    {
        ComputePipelineCreateInfo createInfo;
        createInfo.shaderPath = "../shaders/clear_image.comp.spv";
        createInfo.descriptorTypes = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
        createInfo.pushConstantSize = sizeof(glm::vec4);
        registerPipeline("clear_image", createInfo);
    }

    ComputeDispatchInfo dispatchInfo;
    dispatchInfo.bindings.push_back(ComputeBinding::makeStorageImage(0, imageView));
    dispatchInfo.pushConstants = &clearColor;
    dispatchInfo.pushConstantSize = sizeof(glm::vec4);

    dispatchInfo.groupCountX = 64;
    dispatchInfo.groupCountY = 64;
    dispatchInfo.groupCountZ = 1;

    dispatch(cmd, "clear_image", dispatchInfo);
}

void ComputeManager::copyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, VkDeviceSize size,
                                VkDeviceSize srcOffset, VkDeviceSize dstOffset)
{
    if (!hasPipeline("copy_buffer"))
    {
        ComputePipelineCreateInfo createInfo;
        createInfo.shaderPath = "../shaders/copy_buffer.comp.spv";
        createInfo.descriptorTypes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        createInfo.pushConstantSize = sizeof(uint32_t) * 3;
        registerPipeline("copy_buffer", createInfo);
    }

    ComputeDispatchInfo dispatchInfo;
    dispatchInfo.bindings.push_back(ComputeBinding::storageBuffer(0, src, size, srcOffset));
    dispatchInfo.bindings.push_back(ComputeBinding::storageBuffer(1, dst, size, dstOffset));

    uint32_t pushData[3] = {(uint32_t) size, (uint32_t) srcOffset, (uint32_t) dstOffset};
    dispatchInfo.pushConstants = pushData;
    dispatchInfo.pushConstantSize = sizeof(pushData);

    dispatchInfo.groupCountX = calculateGroupCount(size / 4, 256);
    dispatchInfo.groupCountY = 1;
    dispatchInfo.groupCountZ = 1;

    dispatch(cmd, "copy_buffer", dispatchInfo);
}

bool ComputeManager::createPipeline(const std::string &name, const ComputePipelineCreateInfo &createInfo)
{
    ComputePipeline computePipeline;
    computePipeline.device = engine->_device;

    VkShaderModule shaderModule;
    if (!vkutil::load_shader_module(createInfo.shaderPath.c_str(), engine->_device, &shaderModule))
    {
        std::cerr << "Failed to load compute shader: " << createInfo.shaderPath << std::endl;
        return false;
    }

    if (!createInfo.descriptorTypes.empty())
    {
        DescriptorLayoutBuilder layoutBuilder;
        for (size_t i = 0; i < createInfo.descriptorTypes.size(); ++i)
        {
            layoutBuilder.add_binding(i, createInfo.descriptorTypes[i]);
        }
        computePipeline.descriptorLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();

    if (computePipeline.descriptorLayout != VK_NULL_HANDLE)
    {
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &computePipeline.descriptorLayout;
    }

    VkPushConstantRange pushConstantRange = {};
    if (createInfo.pushConstantSize > 0)
    {
        pushConstantRange.offset = 0;
        pushConstantRange.size = createInfo.pushConstantSize;
        pushConstantRange.stageFlags = createInfo.pushConstantStages;

        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
    }

    VK_CHECK(vkCreatePipelineLayout(engine->_device, &layoutInfo, nullptr, &computePipeline.layout));

    VkPipelineShaderStageCreateInfo stageInfo = vkinit::pipeline_shader_stage_create_info(
        VK_SHADER_STAGE_COMPUTE_BIT, shaderModule);

    VkSpecializationInfo specializationInfo = {};
    if (!createInfo.specializationEntries.empty())
    {
        specializationInfo.mapEntryCount = createInfo.specializationEntries.size();
        specializationInfo.pMapEntries = createInfo.specializationEntries.data();
        specializationInfo.dataSize = createInfo.specializationData.size() * sizeof(uint32_t);
        specializationInfo.pData = createInfo.specializationData.data();
        stageInfo.pSpecializationInfo = &specializationInfo;
    }

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = computePipeline.layout;

    VK_CHECK(
        vkCreateComputePipelines(engine->_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline.pipeline))
    ;

    vkDestroyShaderModule(engine->_device, shaderModule, nullptr);

    pipelines[name] = std::move(computePipeline);

    return true;
}

VkDescriptorSet ComputeManager::allocateDescriptorSet(const ComputePipeline &pipeline,
                                                      const std::vector<ComputeBinding> &bindings)
{
    if (pipeline.descriptorLayout == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    return descriptorAllocator.allocate(engine->_device, pipeline.descriptorLayout);
}

void ComputeManager::updateDescriptorSet(VkDescriptorSet descriptorSet, const std::vector<ComputeBinding> &bindings)
{
    if (descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    DescriptorWriter writer;

    for (const auto &binding: bindings)
    {
        switch (binding.type)
        {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                writer.write_buffer(binding.binding, binding.buffer.buffer, binding.buffer.size,
                                    binding.buffer.offset, binding.type);
                break;

            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                writer.write_image(binding.binding, binding.image.imageView, binding.image.sampler,
                                   binding.image.layout, binding.type);
                break;

            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                writer.write_image(binding.binding, binding.storageImage.imageView, VK_NULL_HANDLE,
                                   binding.storageImage.layout, binding.type);
                break;

            default:
                std::cerr << "Unsupported descriptor type: " << binding.type << std::endl;
                break;
        }
    }

    writer.update_set(engine->_device, descriptorSet);
}

void ComputeManager::insertBarriers(VkCommandBuffer cmd, const ComputeDispatchInfo &dispatchInfo)
{
    if (dispatchInfo.memoryBarriers.empty() &&
        dispatchInfo.bufferBarriers.empty() &&
        dispatchInfo.imageBarriers.empty())
    {
        return;
    }

    VkDependencyInfo dependencyInfo = {};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = dispatchInfo.memoryBarriers.size();
    dependencyInfo.pMemoryBarriers = dispatchInfo.memoryBarriers.data();
    dependencyInfo.bufferMemoryBarrierCount = dispatchInfo.bufferBarriers.size();
    dependencyInfo.pBufferMemoryBarriers = dispatchInfo.bufferBarriers.data();
    dependencyInfo.imageMemoryBarrierCount = dispatchInfo.imageBarriers.size();
    dependencyInfo.pImageMemoryBarriers = dispatchInfo.imageBarriers.data();

    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

# pragma once

#include <vk_types.h>
#include <vk_descriptors.h>
#include <functional>
#include <unordered_map>

class VulkanEngine;

struct ComputeBinding
{
    uint32_t binding;
    VkDescriptorType type;

    union
    {
        struct
        {
            VkBuffer buffer;
            VkDeviceSize offset;
            VkDeviceSize size;
        } buffer;

        struct
        {
            VkImage image;
            VkImageLayout imageLayout;
            VkSampler sampler;
        } image;

        struct
        {
            VkImageView imageView;
            VkImageLayout layout;
        } storageImage;
    };

    static ComputeBinding uniformBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset = 0);

    static ComputeBinding storageBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset = 0);

    static ComputeBinding sampledImage(uint32_t binding, VkImageView imageView, VkSampler sampler,
                                       VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    static ComputeBinding storageImage(uint32_t binding, VkImageView imageView,
                                       VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
};

struct ComputePipelineCreateInfo
{
    std::string shaderPath;
    std::vector<VkDescriptorType> descriptorTypes;
    uint32_t pushConstantSize = 0;
    VkShaderStageFlags pushConstantStages = VK_SHADER_STAGE_COMPUTE_BIT;

    std::vector<VkSpecializationMapEntry> specializationEntries;
    std::vector<uint32_t> specializationData;
};

struct ComputeDispatchInfo
{
    uint32_t groupCountX = 1;
    uint32_t groupCountY = 1;
    uint32_t groupCountZ = 1;

    std::vector<ComputeBinding> bindings;

    const void *pushConstants = nullptr;
    uint32_t pushConstantSize = 0;

    std::vector<VkMemoryBarrier2> memoryBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
};

class ComputePipeline
{
public:
    ComputePipeline() = default;

    ~ComputePipeline();

    ComputePipeline(ComputePipeline &&other) noexcept;

    ComputePipeline &operator=(ComputePipeline &&other) noexcept;

    ComputePipeline(const ComputePipeline &) = delete;

    ComputePipeline &operator=(const ComputePipeline &) = delete;

    bool isValid() const { return pipeline != VK_NULL_HANDLE; }

    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getLayout() const { return layout; }

private:
    friend class ComputeManager;

    VkDevice device = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;

    void cleanup();
};

class ComputeManager
{
public:
    ComputeManager() = default;

    ~ComputeManager();

    void init(VulkanEngine *engine);

    void cleanup();

    bool registerPipeline(const std::string &name, const ComputePipelineCreateInfo &createInfo);
    void unregisterPipeline(const std::string &name);
    bool hasPipeline(const std::string &name) const;

    void dispatch(VkCommandBuffer cmd, const std::string &pipelineName, const ComputeDispatchInfo &dispatchInfo);

    void dispatchImmediate(const std::string &pipelineName, const ComputeDispatchInfo &dispatchInfo);

    static uint32_t calculateGroupCount(uint32_t workItems, uint32_t localSize);
    static ComputeDispatchInfo createDispatch2D(uint32_t width, uint32_t height, uint32_t localSizeX = 16,
                                                uint32_t localSizeY = 16);
    static ComputeDispatchInfo createDispatch3D(uint32_t width, uint32_t height, uint32_t depth,
                                                uint32_t localSizeX = 8, uint32_t localSizeY = 8,
                                                uint32_t localSizeZ = 8);

    void clearImage(VkCommandBuffer cmd, VkImageView imageView, const glm::vec4 &clearColor = {0, 0, 0, 0});
    void copyBuffer(VkCommandBuffer cmd, VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset = 0,
                    VkDeviceSize dstOffset = 0);

private:
    VulkanEngine *engine = nullptr;
    std::unordered_map<std::string, ComputePipeline> pipelines;
    DescriptorAllocatorGrowable descriptorAllocator;

    bool createPipeline(const std::string &name, const ComputePipelineCreateInfo &createInfo);
    VkDescriptorSet allocateDescriptorSet(const ComputePipeline &pipeline, const std::vector<ComputeBinding> &bindings);
    void updateDescriptorSet(VkDescriptorSet descriptorSet, const std::vector<ComputeBinding> &bindings);
    void insertBarriers(VkCommandBuffer cmd, const ComputeDispatchInfo &dispatchInfo);
};

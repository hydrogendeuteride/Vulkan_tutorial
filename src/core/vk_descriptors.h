#pragma once

#include <core/vk_types.h>


struct DescriptorLayoutBuilder
{
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void add_binding(uint32_t binding, VkDescriptorType type);
    void add_binding(uint32_t binding, VkDescriptorType type, uint32_t count);

    void clear();

    VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void *pNext = nullptr,
                                VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorWriter
{
    std::vector<VkDescriptorImageInfo> imageInfos; // must be contiguous for array writes
    std::deque<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkWriteDescriptorSet> writes;

    void write_image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);

    // Write an array of images to a single binding (descriptorCount = infos.size()).
    // The caller must keep the infos alive until update_set is called (we store a copy internally).
    void write_images(int binding, std::span<VkDescriptorImageInfo> infos, VkDescriptorType type);

    void write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

    void clear();

    void update_set(VkDevice device, VkDescriptorSet set);
};


struct DescriptorAllocator
{
    struct PoolSizeRatio
    {
        VkDescriptorType type;
        float ratio;
    };

    VkDescriptorPool pool;

    void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);

    void clear_descriptors(VkDevice device);

    void destroy_pool(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};

struct DescriptorAllocatorGrowable
{
public:
    struct PoolSizeRatio
    {
        VkDescriptorType type;
        float ratio;
    };

    void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);

    void clear_pools(VkDevice device);

    void destroy_pools(VkDevice device);

    VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void *pNext = nullptr);

private:
    VkDescriptorPool get_pool(VkDevice device);

    VkDescriptorPool create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

    std::vector<PoolSizeRatio> ratios;
    std::vector<VkDescriptorPool> fullPools;
    std::vector<VkDescriptorPool> readyPools;
    uint32_t setsPerPool;
};

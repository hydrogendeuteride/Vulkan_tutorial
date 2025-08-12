#pragma once
#include <core/vk_types.h>
#include <functional>

class DeviceManager;

class ResourceManager
{
public:
    void init(DeviceManager *deviceManager);

    void cleanup();

    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;

    void destroy_buffer(const AllocatedBuffer &buffer) const;

    AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage,
                                bool mipmapped = false) const;

    AllocatedImage create_image(const void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage,
                                bool mipmapped = false) const;

    void destroy_image(const AllocatedImage &img) const;

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const;

    void immediate_submit(std::function<void(VkCommandBuffer)> &&function) const;

private:
    DeviceManager *_deviceManager = nullptr;

    // immediate submit structures
    VkFence _immFence = nullptr;
    VkCommandBuffer _immCommandBuffer = nullptr;
    VkCommandPool _immCommandPool = nullptr;

    DeletionQueue _deletionQueue;
};

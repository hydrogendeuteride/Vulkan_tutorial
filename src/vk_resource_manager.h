// vk_resource_manager.h
#pragma once
#include "vk_types.h"
#include <unordered_map>
#include <queue>

#include "vk_loader.h"

template<typename T>
using Handle = uint32_t;

template<typename T>
struct ResourcePool
{
    std::vector<std::unique_ptr<T> > resources;
    std::queue<Handle<T> > freeHandles;
    uint32_t nextHandle = 1;

    Handle<T> allocate(std::unique_ptr<T> resource)
    {
        Handle<T> handle;
        if (!freeHandles.empty())
        {
            handle = freeHandles.front();
            freeHandles.pop();
            resources[handle - 1] = std::move(resource);
        }
        else
        {
            handle = nextHandle++;
            resources.push_back(std::move(resource));
        }
        return handle;
    }

    T *get(Handle<T> handle)
    {
        if (handle == 0 || handle > resources.size()) return nullptr;
        return resources[handle - 1].get();
    }

    void release(Handle<T> handle)
    {
        if (handle == 0 || handle > resources.size()) return;
        resources[handle - 1].reset();
        freeHandles.push(handle);
    }
};

struct MeshResource
{
    GPUMeshBuffers buffers;
    std::vector<GeoSurface> surfaces;
    Bounds bounds;
};

struct TextureResource
{
    AllocatedImage image;
    VkSampler sampler;
};

class ResourceManager
{
private:
    VulkanRenderer *renderer = nullptr;

    ResourcePool<MeshResource> meshPool;
    ResourcePool<TextureResource> texturePool;
    ResourcePool<AllocatedBuffer> bufferPool;
    ResourcePool<GLTFMaterial> materialPool;

    AllocatedImage _whiteImage = {};
    AllocatedImage _blackImage = {};
    AllocatedImage _errorCheckerboardImage = {};
    VkSampler _defaultSamplerLinear = nullptr;
    VkSampler _defaultSamplerNearest = nullptr;

public:
    void init(VulkanRenderer *renderer);

    void cleanup();

    Handle<MeshResource> createMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

    Handle<MeshResource> loadMesh(const std::string &path);
    MeshResource *getMesh(Handle<MeshResource> handle) { return meshPool.get(handle); }

    Handle<TextureResource> createTexture(const void *data, VkExtent3D size, VkFormat format);

    Handle<TextureResource> loadTexture(const std::string &path);

    TextureResource *getTexture(Handle<TextureResource> handle) { return texturePool.get(handle); }

    Handle<AllocatedBuffer> createBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

    AllocatedBuffer *getBuffer(Handle<AllocatedBuffer> handle) { return bufferPool.get(handle); }

    Handle<GLTFMaterial> createMaterial(const MaterialCreateInfo &info);

    GLTFMaterial *getMaterial(Handle<GLTFMaterial> handle) { return materialPool.get(handle); }

    const AllocatedImage &getWhiteImage() const { return _whiteImage; }
    const AllocatedImage &getErrorImage() const { return _errorCheckerboardImage; }
    VkSampler getDefaultSampler() const { return _defaultSamplerLinear; }
};

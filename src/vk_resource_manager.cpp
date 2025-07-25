#include "vk_resource_manager.h"
#include "stb_image.h"
#include "vk_renderer.h"
#include <glm/gtc/packing.hpp>
#include <array>

#include "glm/packing.hpp"

void ResourceManager::init(VulkanRenderer *rendererPtr)
{
    renderer = rendererPtr;

    VkSamplerCreateInfo sampl{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(renderer->_device, &sampl, nullptr, &_defaultSamplerNearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(renderer->_device, &sampl, nullptr, &_defaultSamplerLinear);

    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    _whiteImage = renderer->create_image(&white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    _blackImage = renderer->create_image(&black, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels{};
    for (int x = 0; x < 16; x++)
    {
        for (int y = 0; y < 16; y++)
        {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }

    _errorCheckerboardImage = renderer->create_image(pixels.data(), VkExtent3D{16, 16, 1},
                                                     VK_FORMAT_R8G8B8A8_UNORM,
                                                     VK_IMAGE_USAGE_SAMPLED_BIT);
}

void ResourceManager::cleanup()
{
    for (auto &m : meshPool.resources)
    {
        if (m)
        {
            renderer->destroy_buffer(m->buffers.indexBuffer);
            renderer->destroy_buffer(m->buffers.vertexBuffer);
        }
    }

    for (auto &t : texturePool.resources)
    {
        if (t)
        {
            renderer->destroy_image(t->image);
            if (t->sampler != VK_NULL_HANDLE && t->sampler != _defaultSamplerLinear &&
                t->sampler != _defaultSamplerNearest)
            {
                vkDestroySampler(renderer->_device, t->sampler, nullptr);
            }
        }
    }

    for (auto &b : bufferPool.resources)
    {
        if (b)
        {
            renderer->destroy_buffer(*b);
        }
    }

    vkDestroySampler(renderer->_device, _defaultSamplerLinear, nullptr);
    vkDestroySampler(renderer->_device, _defaultSamplerNearest, nullptr);

    renderer->destroy_image(_whiteImage);
    renderer->destroy_image(_blackImage);
    renderer->destroy_image(_errorCheckerboardImage);
}

Handle<MeshResource> ResourceManager::createMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    auto mesh = std::make_unique<MeshResource>();

    mesh->buffers = renderer->uploadMesh(indices, vertices);

    glm::vec3 minPos = vertices[0].position;
    glm::vec3 maxPos = vertices[0].position;
    for (const auto &v: vertices)
    {
        minPos = glm::min(minPos, v.position);
        maxPos = glm::max(maxPos, v.position);
    }
    mesh->bounds.origin = (maxPos + minPos) / 2.f;
    mesh->bounds.extents = (maxPos - minPos) / 2.f;
    mesh->bounds.sphereRadius = glm::length(mesh->bounds.extents);

    return meshPool.allocate(std::move(mesh));
}

Handle<MeshResource> ResourceManager::loadMesh(const std::string &path)
{
    // simple loader not implemented, return null handle
    (void)path;
    return 0;
}

Handle<TextureResource> ResourceManager::createTexture(const void *data, VkExtent3D size, VkFormat format)
{
    auto tex = std::make_unique<TextureResource>();
    tex->image = renderer->create_image(data, size, format, VK_IMAGE_USAGE_SAMPLED_BIT);
    tex->sampler = _defaultSamplerLinear;
    return texturePool.allocate(std::move(tex));
}

Handle<TextureResource> ResourceManager::loadTexture(const std::string &path)
{
    int w, h, comp;
    stbi_uc *pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels)
    {
        return 0;
    }

    Handle<TextureResource> handle = createTexture(pixels, VkExtent3D{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1},
                                                   VK_FORMAT_R8G8B8A8_UNORM);
    stbi_image_free(pixels);
    return handle;
}

Handle<AllocatedBuffer> ResourceManager::createBuffer(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    auto buf = std::make_unique<AllocatedBuffer>();
    *buf = renderer->create_buffer(size, usage, memoryUsage);
    return bufferPool.allocate(std::move(buf));
}

Handle<GLTFMaterial> ResourceManager::createMaterial(const MaterialCreateInfo &info)
{
    (void)info;
    auto mat = std::make_unique<GLTFMaterial>();
    return materialPool.allocate(std::move(mat));
}
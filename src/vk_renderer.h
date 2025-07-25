#pragma once

#include "vk_compute.h"
#include "vk_types.h"
#include "vk_resource_manager.h"
#include "vk_scene_manager.h"
#include "vk_renderpass_manager.h"
#include "vk_render_view.h"

struct DeletionQueue
{
    std::deque<std::function<void()> > deletors;

    void push_function(std::function<void()> &&function)
    {
        deletors.push_back(function);
    }

    void flush()
    {
        // reverse iterate the deletion queue to execute all the functions
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
        {
            (*it)(); //call functors
        }

        deletors.clear();
    }
};

struct FrameData
{
    VkSemaphore _swapchainSemaphore, _renderSemaphore;
    VkFence _renderFence;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    DeletionQueue _deletionQueue;
    DescriptorAllocatorGrowable _frameDescriptors;

    uint32_t _imageIndex;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanRenderer
{
public:
    VkExtent2D _windowExtent{1920, 1080};

    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debug_messenger;
    VkPhysicalDevice _chosenGPU;
    VkDevice _device;
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;
    VkSurfaceKHR _surface;
    VmaAllocator _allocator;

    std::unique_ptr<ResourceManager> resources;
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<RenderPassManager> renderPassManager;
    ComputeManager compute;

    FrameData _frames[FRAME_OVERLAP];
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent;
    VkExtent2D _drawExtent;

    AllocatedImage _drawImage;
    AllocatedImage _depthImage;

    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;

    struct SDL_Window *_window;

    void init(SDL_Window *window);

    void cleanup();

    void beginFrame();

    void endFrame();

    void draw(const RenderView &view);

    void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function) const;

    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;

    void destroy_buffer(const AllocatedBuffer &buffer) const;

    AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false) const;

    AllocatedImage create_image(const void *data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false) const;

    void destroy_image(const AllocatedImage &img) const;

    GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) const;

    FrameData &get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }

    bool _resizeRequested{false};

private:
    int _frameNumber{0};

    void destroy_swapchain() const;

    void resize_swapchain();

    void init_vulkan();
    void init_swapchain();
    void create_swapchain(uint32_t width, uint32_t height);

    void init_commands();
    void init_sync_structures();
};

#include "vk_renderer.h"

#include <SDL_vulkan.h>

#include "vk_initializers.h"
#include "vk_pipelines.h"
#include <VkBootstrap.h>

#include "vk_images.h"

void VulkanRenderer::init(SDL_Window *window)
{
    _window = window;

    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync_structures();

    resources = std::make_unique<ResourceManager>();
    resources->init(this);


    sceneManager = std::make_unique<SceneManager>();
    sceneManager->init();

    compute.init(this);

    renderPassManager = std::make_unique<RenderPassManager>();
    renderPassManager->init(this);
}

void VulkanRenderer::cleanup()
{
    vkDeviceWaitIdle(_device);

    if (renderPassManager) renderPassManager->cleanup();
    if (sceneManager) sceneManager->cleanup();
    if (resources) resources->cleanup();
    compute.cleanup();

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
        vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
        vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
        vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);
        _frames[i]._deletionQueue.flush();
    }

    destroy_swapchain();
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vmaDestroyAllocator(_allocator);
    vkDestroyDevice(_device, nullptr);
    vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
    vkDestroyInstance(_instance, nullptr);
}

void VulkanRenderer::beginFrame()
{
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

    const auto &frame = get_current_frame();
    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);

    uint32_t imageIndex;
    const VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1'000'000'000,
                                             frame._swapchainSemaphore,
                                             VK_NULL_HANDLE, &imageIndex);

    if (e == VK_ERROR_OUT_OF_DATE_KHR)
    {
        _resizeRequested = true;
        return;
    }

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));

    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
}

void VulkanRenderer::endFrame()
{
    const auto &frame = get_current_frame();
    VkCommandBuffer cmd = frame._mainCommandBuffer;
    const uint32_t imageIndex = frame._imageIndex;

    vkutil::transition_image(cmd, _swapchainImages[imageIndex],
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        get_current_frame()._renderSemaphore);

    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    VkPresentInfoKHR presentInfo = vkinit::present_info();
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        _resizeRequested = true;
    }

    _frameNumber++;
}

void VulkanRenderer::draw(const RenderView &view)
{
    if (_resizeRequested)
    {
        resize_swapchain();
        return;
    }

    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    if (sceneManager->getActiveCamera())
    {
        sceneManager->updateSceneData(view);
    }

    renderPassManager->executeAll(cmd, _drawExtent, sceneManager.get(), resources.get());
}

void VulkanRenderer::init_vulkan()
{
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("Vulkan Renderer")
            .request_validation_layers(true)
            .use_default_debug_messenger()
            .require_api_version(1, 3, 0)
            .build();

    vkb::Instance vkb_inst = inst_ret.value();
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    VkPhysicalDeviceVulkan13Features features{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = true;
    features.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector{vkb_inst};
    vkb::PhysicalDevice physicalDevice = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(_surface)
            .select()
            .value();

    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();

    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;
    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // VMA 초기화
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void VulkanRenderer::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);

    VkExtent3D drawImageExtent =
    {
        _windowExtent.width,
        _windowExtent.height,
        1
    };

    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = vkinit::image_create_info(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_allocator, &rimg_info, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    VkImageViewCreateInfo rview_info = vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image,
                                                                     VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView));

    // Depth image
    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;
    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthImage.imageFormat, depthImageUsages, drawImageExtent);
    vmaCreateImage(_allocator, &dimg_info, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image,
                                                                     VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView));
}

void VulkanRenderer::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{_chosenGPU, _device, _surface};

    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
            //.use_default_format_selection()
            .set_desired_format(VkSurfaceFormatKHR{
                .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
            })
            //use vsync present mode
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

    _swapchainExtent = vkbSwapchain.extent;
    //store swapchain and its related images
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanRenderer::init_commands()
{
    const VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (auto &_frame: _frames)
    {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frame._commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frame._commandPool, 1);
        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frame._mainCommandBuffer));

        // Frame descriptor 초기화
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };

        _frame._frameDescriptors.init(_device, 1000, frame_sizes);
    }

    // Immediate submit용 커맨드 풀
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));
}

void VulkanRenderer::init_sync_structures()
{
    const VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    const VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; ++i)
    {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
    }

    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
}

// 기존 immediate_submit 그대로 사용
void VulkanRenderer::immediate_submit(std::function<void(VkCommandBuffer)> &&function) const
{
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer cmd = _immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    function(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

// 기존 helper 함수들
AllocatedBuffer VulkanRenderer::create_buffer(size_t allocSize, VkBufferUsageFlags usage,
                                              VmaMemoryUsage memoryUsage) const
{
    VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    AllocatedBuffer newBuffer{};

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer,
        &newBuffer.allocation, &newBuffer.info));

    return newBuffer;
}

void VulkanRenderer::destroy_buffer(const AllocatedBuffer &buffer) const
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void VulkanRenderer::destroy_swapchain() const
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    for (auto _swapchainImageView: _swapchainImageViews)
    {
        vkDestroyImageView(_device, _swapchainImageView, nullptr);
    }
}

void VulkanRenderer::resize_swapchain()
{
    vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    _resizeRequested = false;
}

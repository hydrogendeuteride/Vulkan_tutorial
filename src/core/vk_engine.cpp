//> includes
#include "vk_engine.h"
#include <core/vk_images.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_vulkan.h"

#include <core/vk_initializers.h>
#include <core/vk_types.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

#include "render/vk_pipelines.h"
#include <iostream>
#include <glm/gtx/transform.hpp>
#include "render/primitives.h"

#include "vk_mem_alloc.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "render/vk_renderpass_geometry.h"
#include "render/vk_renderpass_imgui.h"
#include "render/vk_renderpass_lighting.h"
#include "vk_resource.h"
#include "engine_context.h"
#include "core/vk_pipeline_manager.h"

VulkanEngine *loadedEngine = nullptr;

void VulkanEngine::init()
{
    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    constexpr auto window_flags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    _swapchainManager = std::make_unique<SwapchainManager>();

    _window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _swapchainManager->windowExtent().width,
        _swapchainManager->windowExtent().height,
        window_flags
    );

    _deviceManager = std::make_shared<DeviceManager>();
    _deviceManager->init_vulkan(_window);

    _resourceManager = std::make_shared<ResourceManager>();
    _resourceManager->init(_deviceManager.get());

    _descriptorManager = std::make_unique<DescriptorManager>();
    _descriptorManager->init(_deviceManager.get());

    _samplerManager = std::make_unique<SamplerManager>();
    _samplerManager->init(_deviceManager.get());

    // Build dependency-injection context
    _context = std::make_shared<EngineContext>();
    _context->device = _deviceManager;
    _context->resources = _resourceManager;
    _context->descriptors = std::make_shared<DescriptorAllocatorGrowable>(); {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };
        _context->descriptors->init(_deviceManager->device(), 10, sizes);
    }

    _swapchainManager->init(_deviceManager.get(), _resourceManager.get());
    _swapchainManager->init_swapchain();

    // Fill remaining context pointers now that managers exist
    _context->descriptorLayouts = _descriptorManager.get();
    _context->samplers = _samplerManager.get();
    _context->swapchain = _swapchainManager.get();

    // Create graphics pipeline manager (after swapchain is ready)
    _pipelineManager = std::make_unique<PipelineManager>();
    _pipelineManager->init(_context.get());
    _context->pipelines = _pipelineManager.get();

    _sceneManager = std::make_unique<SceneManager>();
    _sceneManager->init(_context.get());
    _context->scene = _sceneManager.get();

    compute.init(_context.get());
    // Publish engine-owned subsystems into context for modules
    _context->compute = &compute;
    _context->window = _window;
    _context->stats = &stats;

    init_frame_resources();

    init_pipelines();

    init_default_data();

    _renderPassManager = std::make_unique<RenderPassManager>();
    _renderPassManager->init(_context.get());

    auto imguiPass = std::make_unique<ImGuiPass>();
    _renderPassManager->setImGuiPass(std::move(imguiPass));

    const std::string structurePath = {"../assets/structure.glb"};
    const auto structureFile = loadGltf(this, structurePath);

    assert(structureFile.has_value());

    _sceneManager->loadScene("structure", *structureFile);

    //everything went fine
    _isInitialized = true;
}

void VulkanEngine::init_default_data()
{
    //> default_img
    //3 default textures, white, grey, black. 1 pixel each
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    _whiteImage = _resourceManager->create_image((void *) &white, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                                 VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    _greyImage = _resourceManager->create_image((void *) &grey, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                                VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    _blackImage = _resourceManager->create_image((void *) &black, VkExtent3D{1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM,
                                                 VK_IMAGE_USAGE_SAMPLED_BIT);

    //checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels{}; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++)
    {
        for (int y = 0; y < 16; y++)
        {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _errorCheckerboardImage = _resourceManager->create_image(pixels.data(), VkExtent3D{16, 16, 1},
                                                             VK_FORMAT_R8G8B8A8_UNORM,
                                                             VK_IMAGE_USAGE_SAMPLED_BIT);

    //create a simple white material that we can use for generated meshes
    GLTFMetallic_Roughness::MaterialResources matResources{};
    matResources.colorImage = _whiteImage;
    matResources.colorSampler = _samplerManager->defaultLinear();
    matResources.metalRoughImage = _whiteImage;
    matResources.metalRoughSampler = _samplerManager->defaultLinear();

    AllocatedBuffer matBuffer = _resourceManager->create_buffer(sizeof(GLTFMetallic_Roughness::MaterialConstants),
                                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                VMA_MEMORY_USAGE_CPU_TO_GPU);

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(_deviceManager->allocator(), matBuffer.allocation, &allocInfo);
    auto *matConstants = (GLTFMetallic_Roughness::MaterialConstants *) allocInfo.pMappedData;
    *matConstants = {};
    matConstants->colorFactors = glm::vec4(1.0f);
    matResources.dataBuffer = matBuffer.buffer;
    matResources.dataBufferOffset = 0;

    auto defaultMaterial = std::make_shared<GLTFMaterial>();
    defaultMaterial->data = metalRoughMaterial.write_material(
        _deviceManager->device(), MaterialPass::MainColor,
        matResources, *_context->descriptors);

    //build cube mesh
    {
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;
        primitives::buildCube(verts, inds);

        cubeMesh = std::make_shared<MeshAsset>();
        cubeMesh->name = "Cube";
        cubeMesh->meshBuffers = _resourceManager->uploadMesh(inds, verts);

        GeoSurface surf{};
        surf.startIndex = 0;
        surf.count = (uint32_t) inds.size();
        surf.material = defaultMaterial;
        surf.bounds.origin = glm::vec3(0.0f);
        surf.bounds.extents = glm::vec3(0.5f);
        surf.bounds.sphereRadius = glm::length(surf.bounds.extents);
        cubeMesh->surfaces.push_back(surf);
    }

    //build sphere mesh
    {
        std::vector<Vertex> verts;
        std::vector<uint32_t> inds;
        primitives::buildSphere(verts, inds);

        sphereMesh = std::make_shared<MeshAsset>();
        sphereMesh->name = "Sphere";
        sphereMesh->meshBuffers = _resourceManager->uploadMesh(inds, verts);

        GeoSurface surf{};
        surf.startIndex = 0;
        surf.count = static_cast<uint32_t>(inds.size());
        surf.material = defaultMaterial;
        surf.bounds.origin = glm::vec3(0.0f);
        surf.bounds.extents = glm::vec3(0.5f);
        surf.bounds.sphereRadius = glm::length(surf.bounds.extents);
        sphereMesh->surfaces.push_back(surf);
    }

    _mainDeletionQueue.push_function([&]() {
        // Clean up mesh buffers
        if (cubeMesh)
        {
            _resourceManager->destroy_buffer(cubeMesh->meshBuffers.indexBuffer);
            _resourceManager->destroy_buffer(cubeMesh->meshBuffers.vertexBuffer);
        }
        if (sphereMesh)
        {
            _resourceManager->destroy_buffer(sphereMesh->meshBuffers.indexBuffer);
            _resourceManager->destroy_buffer(sphereMesh->meshBuffers.vertexBuffer);
        }
    });

    // Expose default meshes to context for modules that need them
    _context->cubeMesh = cubeMesh;
    _context->sphereMesh = sphereMesh;

    _mainDeletionQueue.push_function([=]() { _resourceManager->destroy_buffer(matBuffer); });

    _mainDeletionQueue.push_function([&]() {
        _resourceManager->destroy_image(_whiteImage);
        _resourceManager->destroy_image(_greyImage);
        _resourceManager->destroy_image(_blackImage);
        _resourceManager->destroy_image(_errorCheckerboardImage);
    });
    //< default_img
}

void VulkanEngine::cleanup()
{
    vkDeviceWaitIdle(_deviceManager->device());

    _sceneManager->cleanup();

    if (_isInitialized)
    {
        //make sure the gpu has stopped doing its things
        vkDeviceWaitIdle(_deviceManager->device());

        // Flush all frame deletion queues first while VMA allocator is still alive
        for (int i = 0; i < FRAME_OVERLAP; i++)
        {
            _frames[i]._deletionQueue.flush();
        }
        for (int i = 0; i < FRAME_OVERLAP; i++)
        {
            _frames[i].cleanup(_deviceManager.get());
        }

        metalRoughMaterial.clear_resources(_deviceManager->device());

        _mainDeletionQueue.flush();

        _renderPassManager->cleanup();

        _pipelineManager->cleanup();

        compute.cleanup();

        _swapchainManager->cleanup();

        _resourceManager->cleanup();

        _samplerManager->cleanup();
        _descriptorManager->cleanup();

        _context->descriptors->destroy_pools(_deviceManager->device());

        _deviceManager->cleanup();

        SDL_DestroyWindow(_window);
    }
}

void VulkanEngine::draw()
{
    _sceneManager->update_scene();
    //> frame_clear
    //wait until the gpu has finished rendering the last frame. Timeout of 1 second
    VK_CHECK(vkWaitForFences(_deviceManager->device(), 1, &get_current_frame()._renderFence, true, 1000000000));

    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_deviceManager->device());
    //< frame_clear

    uint32_t swapchainImageIndex;

    VkResult e = vkAcquireNextImageKHR(_deviceManager->device(), _swapchainManager->swapchain(), 1000000000,
                                       get_current_frame()._swapchainSemaphore,
                                       nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR)
    {
        resize_requested = true;
        return;
    }

    _drawExtent.height = std::min(_swapchainManager->swapchainExtent().height,
                                  _swapchainManager->drawImage().imageExtent.height) * renderScale;
    _drawExtent.width = std::min(_swapchainManager->swapchainExtent().width,
                                 _swapchainManager->drawImage().imageExtent.width) * renderScale;

    VK_CHECK(vkResetFences(_deviceManager->device(), 1, &get_current_frame()._renderFence));

    //now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
    VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));

    //naming it cmd for shorter writing
    VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

    //begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    //---------------------------
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // publish per-frame pointers and draw extent to context for passes
    _context->currentFrame = &get_current_frame();
    _context->drawExtent = _drawExtent;

    // Optional: check for shader changes and hot-reload pipelines
    if (_pipelineManager)
    {
        _pipelineManager->hotReloadChanged();
    }

    _renderPassManager->executeAll(cmd);

    //transtion the draw image and the swapchain image into their correct transfer layouts
    vkutil::transition_image(cmd, _swapchainManager->drawImage().image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, _swapchainManager->swapchainImages()[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    //--------------------
    //> copyimage
    vkutil::copy_image_to_image(cmd, _swapchainManager->drawImage().image,
                                _swapchainManager->swapchainImages()[swapchainImageIndex], _drawExtent,
                                _swapchainManager->swapchainExtent());
    //< copyimage

    vkutil::transition_image(cmd, _swapchainManager->swapchainImages()[swapchainImageIndex],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    _renderPassManager->executeImGui(cmd, _swapchainManager->swapchainImageViews()[swapchainImageIndex]);

    vkutil::transition_image(cmd, _swapchainManager->swapchainImages()[swapchainImageIndex],
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
                                                                   get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                                                     get_current_frame()._renderSemaphore);

    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

    VK_CHECK(vkQueueSubmit2(_deviceManager->graphicsQueue(), 1, &submit, get_current_frame()._renderFence));

    VkPresentInfoKHR presentInfo = vkinit::present_info();

    VkSwapchainKHR swapchain = _swapchainManager->swapchain();
    presentInfo.pSwapchains = &swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(_deviceManager->graphicsQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        resize_requested = true;
    }

    _frameNumber++;
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    //main loop
    while (!bQuit)
    {
        auto start = std::chrono::system_clock::now();
        //Handle events on queue
        while (SDL_PollEvent(&e) != 0)
        {
            //close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT) bQuit = true;
            if (e.type == SDL_WINDOWEVENT)
            {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
                {
                    freeze_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED)
                {
                    freeze_rendering = false;
                }
            }
            _sceneManager->getMainCamera().processSDLEvent(e);
            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        if (freeze_rendering)
        {
            //throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (resize_requested)
        {
            _swapchainManager->resize_swapchain(_window);
        }


        // imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();

        if (ImGui::Begin("background"))
        {
            auto background_pass = _renderPassManager->getPass<BackgroundPass>();
            ComputeEffect &selected = background_pass->_backgroundEffects[background_pass->_currentEffect];

            ImGui::Text("Selected effect: %s", selected.name);

            ImGui::SliderInt("Effect Index", &background_pass->_currentEffect, 0,
                             background_pass->_backgroundEffects.size() - 1);

            ImGui::InputFloat4("data1", reinterpret_cast<float *>(&selected.data.data1));
            ImGui::InputFloat4("data2", reinterpret_cast<float *>(&selected.data.data2));
            ImGui::InputFloat4("data3", reinterpret_cast<float *>(&selected.data.data3));
            ImGui::InputFloat4("data4", reinterpret_cast<float *>(&selected.data.data4));

            ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.f);

            ImGui::End();
        }

        if (ImGui::Begin("Stats"))
        {
            ImGui::Text("frametime %f ms", stats.frametime);
            ImGui::Text("draw time %f ms", stats.mesh_draw_time);
            ImGui::Text("update time %f ms", _sceneManager->stats.scene_update_time);
            ImGui::Text("triangles %i", stats.triangle_count);
            ImGui::Text("draws %i", stats.drawcall_count);
            ImGui::End();
        }
        ImGui::Render();
        draw();

        auto end = std::chrono::system_clock::now();

        //convert to microseconds (integer), and then come back to miliseconds
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        stats.frametime = elapsed.count() / 1000.f;
    }
}

void VulkanEngine::init_frame_resources()
{
    // descriptor pool sizes per-frame
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
    };

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        _frames[i].init(_deviceManager.get(), frame_sizes);
    }
}

void VulkanEngine::init_pipelines()
{
    metalRoughMaterial.build_pipelines(this);
}

void MeshNode::Draw(const glm::mat4 &topMatrix, DrawContext &ctx)
{
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto &s: mesh->surfaces)
    {
        RenderObject def{};
        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        def.material = &s.material->data;

        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        if (s.material->data.passType == MaterialPass::Transparent)
        {
            ctx.TransparentSurfaces.push_back(def);
        }
        else
        {
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    // recurse down
    Node::Draw(topMatrix, ctx);
}

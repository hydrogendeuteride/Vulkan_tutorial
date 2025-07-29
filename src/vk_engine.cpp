//> includes
#include "vk_engine.h"
#include <vk_images.h>

#include "SDL2/SDL.h"
#include "SDL2/SDL_vulkan.h"

#include <vk_initializers.h>
#include <vk_types.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

#include <vk_pipelines.h>
#include <iostream>
#include <glm/gtx/transform.hpp>
#include "primitives.h"

#include "vk_mem_alloc.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "vk_renderpass_geometry.h"
#include "vk_renderpass_imgui.h"
#include "vk_renderpass_lighting.h"
#include "vk_resource.h"

constexpr bool bUseValidationLayers = false;

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

    _deviceManager = std::make_unique<DeviceManager>();
    _deviceManager->init_vulkan(_window);

    _resourceManager = std::make_unique<ResourceManager>();
    _resourceManager->init(_deviceManager.get());

    _swapchainManager->init(_deviceManager.get(), _resourceManager.get());
    _swapchainManager->init_swapchain();

    _renderPassManager = std::make_unique<RenderPassManager>();

    compute.init(this);

    _renderPassManager->init(this);

    auto imguiPass = std::make_unique<ImGuiPass>();
    _renderPassManager->setImGuiPass(std::move(imguiPass));

    init_commands();

    init_sync_structures();

    init_default_samplers();

    init_descriptors();

    init_pipelines();

    init_default_data();

    mainCamera.velocity = glm::vec3(0.f);
    mainCamera.position = glm::vec3(30.f, -00.f, -085.f);

    mainCamera.pitch = 0;
    mainCamera.yaw = 0;

    const std::string structurePath = {"../assets/structure.glb"};
    const auto structureFile = loadGltf(this, structurePath);

    assert(structureFile.has_value());

    loadedScenes["structure"] = *structureFile;

    sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    sceneData.sunlightDirection = glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f);
    sceneData.sunlightColor = glm::vec4(1.0f, 1.0f, 1.0f, 3.0f);

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
    matResources.colorSampler = _defaultSamplerLinear;
    matResources.metalRoughImage = _whiteImage;
    matResources.metalRoughSampler = _defaultSamplerLinear;

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
    defaultMaterial->data = metalRoughMaterial.write_material(_deviceManager->device(), MaterialPass::MainColor,
                                                              matResources, globalDescriptorAllocator);

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

    loadedScenes.clear();

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
            vkDestroyCommandPool(_deviceManager->device(), _frames[i]._commandPool, nullptr);

            //destroy sync objects
            vkDestroyFence(_deviceManager->device(), _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_deviceManager->device(), _frames[i]._renderSemaphore, nullptr);
            vkDestroySemaphore(_deviceManager->device(), _frames[i]._swapchainSemaphore, nullptr);
        }

        metalRoughMaterial.clear_resources(_deviceManager->device());

        _mainDeletionQueue.flush();

        _renderPassManager->cleanup();

        compute.cleanup();

        _swapchainManager->cleanup();

        _resourceManager->cleanup();

        _deviceManager->cleanup();

        SDL_DestroyWindow(_window);
    }
}

void VulkanEngine::draw()
{
    update_scene();
    //> frame_clear
    //wait until the gpu has finished rendering the last frame. Timeout of 1 second
    VK_CHECK(vkWaitForFences(_deviceManager->device(), 1, &get_current_frame()._renderFence, true, 1000000000));

    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_deviceManager->device());
    //< frame_clear

    get_current_frame()._deletionQueue.flush();

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
            mainCamera.processSDLEvent(e);
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

            ImGui::Text("Selected effect: ", selected.name);

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
            ImGui::Text("update time %f ms", stats.scene_update_time);
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

void VulkanEngine::update_scene()
{
    mainDrawContext.OpaqueSurfaces.clear();
    mainDrawContext.TransparentSurfaces.clear();

    mainCamera.update();

    loadedScenes["structure"]->Draw(glm::mat4{1.f}, mainDrawContext);

    if (cubeMesh)
    {
        const GeoSurface &surf = cubeMesh->surfaces[0];
        RenderObject obj{};
        obj.indexCount = surf.count;
        obj.firstIndex = surf.startIndex;
        obj.indexBuffer = cubeMesh->meshBuffers.indexBuffer.buffer;
        obj.vertexBufferAddress = cubeMesh->meshBuffers.vertexBufferAddress;
        obj.material = &surf.material->data;
        obj.bounds = surf.bounds;
        obj.transform = glm::translate(glm::mat4(1.f), glm::vec3(-2.f, 0.f, -2.f));
        mainDrawContext.OpaqueSurfaces.push_back(obj);
    }

    if (sphereMesh)
    {
        const auto &[startIndex, count, bounds, material] = sphereMesh->surfaces[0];
        RenderObject obj{};
        obj.indexCount = count;
        obj.firstIndex = startIndex;
        obj.indexBuffer = sphereMesh->meshBuffers.indexBuffer.buffer;
        obj.vertexBufferAddress = sphereMesh->meshBuffers.vertexBufferAddress;
        obj.material = &material->data;
        obj.bounds = bounds;
        obj.transform = glm::translate(glm::mat4(1.f), glm::vec3(2.f, 0.f, -2.f));
        mainDrawContext.OpaqueSurfaces.push_back(obj);
    }

    glm::mat4 view = mainCamera.getViewMatrix();

    // camera projection
    glm::mat4 projection = glm::perspective(glm::radians(70.f),
                                            (float) _swapchainManager->windowExtent().width / (float) _swapchainManager
                                            ->windowExtent().height, 10000.f, 0.1f);

    // invert the Y direction on projection matrix so that we are more similar
    // to opengl and gltf axis
    projection[1][1] *= -1;

    sceneData.view = view;
    sceneData.proj = projection;
    sceneData.viewproj = projection * view;
}

void VulkanEngine::init_commands()
{
    const VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(
        _deviceManager->graphicsQueueFamily(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    for (auto &_frame: _frames)
    {
        VK_CHECK(vkCreateCommandPool(_deviceManager->device(), &commandPoolInfo, nullptr, &_frame._commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frame._commandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(_deviceManager->device(), &cmdAllocInfo, &_frame._mainCommandBuffer));
    }
}

void VulkanEngine::init_sync_structures()
{
    const VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    const VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < FRAME_OVERLAP; ++i)
    {
        VK_CHECK(vkCreateFence(_deviceManager->device(), &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

        VK_CHECK(
            vkCreateSemaphore(_deviceManager->device(), &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore))
        ;
        VK_CHECK(
            vkCreateSemaphore(_deviceManager->device(), &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));
    }
}

void VulkanEngine::init_default_samplers()
{
    VkSamplerCreateInfo sampl = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(_deviceManager->device(), &sampl, nullptr, &_defaultSamplerNearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_deviceManager->device(), &sampl, nullptr, &_defaultSamplerLinear);

    _mainDeletionQueue.push_function([&]() {
        vkDestroySampler(_deviceManager->device(), _defaultSamplerNearest, nullptr);
        vkDestroySampler(_deviceManager->device(), _defaultSamplerLinear, nullptr);
    });
}

void VulkanEngine::init_pipelines()
{
    init_mesh_pipeline();

    metalRoughMaterial.build_pipelines(this);

    init_deferred_pipelines();
}

void VulkanEngine::init_descriptors()
{
    //create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
    {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}
    };

    globalDescriptorAllocator.init(_deviceManager->device(), 10, sizes);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _singleImageDescriptorLayout = builder.build(_deviceManager->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
    } {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _gpuSceneDataDescriptorLayout = builder.build(
            _deviceManager->device(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    } {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _gBufferInputDescriptorLayout = builder.build(_deviceManager->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    _gBufferInputDescriptorSet = globalDescriptorAllocator.allocate(_deviceManager->device(),
                                                                    _gBufferInputDescriptorLayout); {
        DescriptorWriter writer;
        writer.write_image(0, _swapchainManager->gBufferPosition().imageView, _defaultSamplerLinear,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(1, _swapchainManager->gBufferNormal().imageView, _defaultSamplerLinear,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(2, _swapchainManager->gBufferAlbedo().imageView, _defaultSamplerLinear,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.update_set(_deviceManager->device(), _gBufferInputDescriptorSet);
    }

    //make sure both the descriptor allocator and the new layout get cleaned up properly
    _mainDeletionQueue.push_function([&]() {
        globalDescriptorAllocator.destroy_pools(_deviceManager->device());

        vkDestroyDescriptorSetLayout(_deviceManager->device(), _singleImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_deviceManager->device(), _gpuSceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_deviceManager->device(), _gBufferInputDescriptorLayout, nullptr);
    });

    //> frame_desc
    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        // create a descriptor pool
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        };

        _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
        _frames[i]._frameDescriptors.init(_deviceManager->device(), 1000, frame_sizes);

        _mainDeletionQueue.push_function([&, i]() {
            _frames[i]._frameDescriptors.destroy_pools(_deviceManager->device());
        });
    }
    //< frame_desc
}

void GLTFMetallic_Roughness::build_pipelines(VulkanEngine *engine)
{
    VkShaderModule meshFragShader;
    if (!vkutil::load_shader_module("../shaders/mesh.frag.spv", engine->_deviceManager->device(), &meshFragShader))
    {
        fmt::println("Error when building the triangle fragment shader module");
    }

    VkShaderModule meshVertexShader;
    if (!vkutil::load_shader_module("../shaders/mesh.vert.spv", engine->_deviceManager->device(), &meshVertexShader))
    {
        fmt::println("Error when building the triangle vertex shader module");
    }

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(engine->_deviceManager->device(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = {
        engine->_gpuSceneDataDescriptorLayout,
        materialLayout
    };

    VkPipelineLayoutCreateInfo mesh_layout_info = vkinit::pipeline_layout_create_info();
    mesh_layout_info.setLayoutCount = 2;
    mesh_layout_info.pSetLayouts = layouts;
    mesh_layout_info.pPushConstantRanges = &matrixRange;
    mesh_layout_info.pushConstantRangeCount = 1;

    VkPipelineLayout newLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_deviceManager->device(), &mesh_layout_info, nullptr, &newLayout));

    opaquePipeline.layout = newLayout;
    transparentPipeline.layout = newLayout;

    // build the stage-create-info for both vertex and fragment stages. This lets
    // the pipeline know the shader modules per stage
    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //render format
    pipelineBuilder.set_color_attachment_format(engine->_swapchainManager->drawImage().imageFormat);
    pipelineBuilder.set_depth_format(engine->_swapchainManager->depthImage().imageFormat);

    // use the triangle layout we created
    pipelineBuilder._pipelineLayout = newLayout;

    // finally build the pipeline
    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_deviceManager->device());

    // create the transparent variant
    pipelineBuilder.enable_blending_additive();

    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.pipeline = pipelineBuilder.build_pipeline(engine->_deviceManager->device());

    VkShaderModule gbufferFragShader;
    bool gbufferLoaded = vkutil::load_shader_module("../shaders/gbuffer.frag.spv", engine->_deviceManager->device(),
                                                    &gbufferFragShader);
    if (!gbufferLoaded)
    {
        fmt::println("Failed to load gbuffer fragment shader");
        vkDestroyShaderModule(engine->_deviceManager->device(), meshFragShader, nullptr);
        vkDestroyShaderModule(engine->_deviceManager->device(), meshVertexShader, nullptr);
        return;
    }

    PipelineBuilder gbufferBuilder;
    gbufferBuilder.set_shaders(meshVertexShader, gbufferFragShader);
    gbufferBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    gbufferBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    gbufferBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    gbufferBuilder.set_multisampling_none();
    gbufferBuilder.disable_blending();
    gbufferBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    VkFormat gFormats[] = {
        engine->_swapchainManager->gBufferPosition().imageFormat,
        engine->_swapchainManager->gBufferNormal().imageFormat, engine->_swapchainManager->gBufferAlbedo().imageFormat
    };
    gbufferBuilder.set_color_attachment_formats(std::span<VkFormat>(gFormats, 3));
    gbufferBuilder.set_depth_format(engine->_swapchainManager->depthImage().imageFormat);
    gbufferBuilder._pipelineLayout = newLayout;
    gBufferPipeline.pipeline = gbufferBuilder.build_pipeline(engine->_deviceManager->device());
    gBufferPipeline.layout = newLayout;

    vkDestroyShaderModule(engine->_deviceManager->device(), gbufferFragShader, nullptr);

    vkDestroyShaderModule(engine->_deviceManager->device(), meshFragShader, nullptr);
    vkDestroyShaderModule(engine->_deviceManager->device(), meshVertexShader, nullptr);
}

void GLTFMetallic_Roughness::clear_resources(VkDevice device) const
{
    vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
    vkDestroyPipelineLayout(device, transparentPipeline.layout, nullptr);

    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
    vkDestroyPipeline(device, gBufferPipeline.pipeline, nullptr);
}

MaterialInstance GLTFMetallic_Roughness::write_material(VkDevice device, MaterialPass pass,
                                                        const MaterialResources &resources,
                                                        DescriptorAllocatorGrowable &descriptorAllocator)
{
    MaterialInstance matData{};
    matData.passType = pass;
    if (pass == MaterialPass::Transparent)
    {
        matData.pipeline = &transparentPipeline;
    }
    else
    {
        matData.pipeline = &gBufferPipeline;
    }

    matData.materialSet = descriptorAllocator.allocate(device, materialLayout);


    writer.clear();
    writer.write_buffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.write_image(1, resources.colorImage.imageView, resources.colorSampler,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(2, resources.metalRoughImage.imageView, resources.metalRoughSampler,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    writer.update_set(device, matData.materialSet);

    return matData;
}

void VulkanEngine::init_mesh_pipeline()
{
    VkShaderModule triangleFragShader;
    if (!vkutil::load_shader_module("../shaders/text_image.frag.spv", _deviceManager->device(), &triangleFragShader))
    {
        fmt::print("Error when building the triangle fragment shader module");
    }
    else
    {
        fmt::print("Triangle fragment shader succesfully loaded");
    }

    VkShaderModule triangleVertexShader;
    if (!vkutil::load_shader_module("../shaders/colored_triangle_mesh.vert.spv", _deviceManager->device(),
                                    &triangleVertexShader))
    {
        fmt::print("Error when building the triangle vertex shader module");
    }
    else
    {
        fmt::print("Triangle vertex shader succesfully loaded");
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pSetLayouts = &_singleImageDescriptorLayout;
    pipeline_layout_info.setLayoutCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_deviceManager->device(), &pipeline_layout_info, nullptr, &_meshPipelineLayout));

    PipelineBuilder pipelineBuilder;

    //use the triangle layout we created
    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    //connecting the vertex and pixel shaders to the pipeline
    pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    //it will draw triangles
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //filled triangles
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //no backface culling
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //no multisampling
    pipelineBuilder.set_multisampling_none();
    //no blending
    //pipelineBuilder.disable_blending();
    pipelineBuilder.disable_blending();
    //pipelineBuilder.enable_blending_additive();
    //no depth testing
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    //connect the image format we will draw into, from draw image
    pipelineBuilder.set_color_attachment_format(_swapchainManager->drawImage().imageFormat);
    pipelineBuilder.set_depth_format(_swapchainManager->depthImage().imageFormat);

    //finally build the pipeline
    _meshPipeline = pipelineBuilder.build_pipeline(_deviceManager->device());

    //clean structures
    vkDestroyShaderModule(_deviceManager->device(), triangleFragShader, nullptr);
    vkDestroyShaderModule(_deviceManager->device(), triangleVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_deviceManager->device(), _meshPipelineLayout, nullptr);
        vkDestroyPipeline(_deviceManager->device(), _meshPipeline, nullptr);
    });
}

void VulkanEngine::init_deferred_pipelines()
{
    VkShaderModule vertShader;
    VkShaderModule gbufferFrag;
    VkShaderModule fullscreenVert;
    VkShaderModule lightingFrag;

    bool vertLoaded = vkutil::load_shader_module("../shaders/mesh.vert.spv", _deviceManager->device(), &vertShader);
    bool gbufferLoaded = vkutil::load_shader_module("../shaders/gbuffer.frag.spv", _deviceManager->device(),
                                                    &gbufferFrag);
    bool fullscreenLoaded = vkutil::load_shader_module("../shaders/fullscreen.vert.spv", _deviceManager->device(),
                                                       &fullscreenVert);
    bool lightingLoaded = vkutil::load_shader_module("../shaders/deferred_lighting.frag.spv", _deviceManager->device(),
                                                     &lightingFrag);

    if (!vertLoaded || !gbufferLoaded || !fullscreenLoaded || !lightingLoaded)
    {
        fmt::println("Failed to load deferred rendering shaders");
        return;
    }

    VkPushConstantRange range{};
    range.offset = 0;
    range.size = sizeof(GPUDrawPushConstants);
    range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayout gLayouts[] = {_gpuSceneDataDescriptorLayout, metalRoughMaterial.materialLayout};
    VkPipelineLayoutCreateInfo meshLayoutInfo = vkinit::pipeline_layout_create_info();
    meshLayoutInfo.setLayoutCount = 2;
    meshLayoutInfo.pSetLayouts = gLayouts;
    meshLayoutInfo.pPushConstantRanges = &range;
    meshLayoutInfo.pushConstantRangeCount = 1;
    VK_CHECK(vkCreatePipelineLayout(_deviceManager->device(), &meshLayoutInfo, nullptr, &_gBufferPipelineLayout));

    PipelineBuilder builder;
    builder._pipelineLayout = _gBufferPipelineLayout;
    builder.set_shaders(vertShader, gbufferFrag);
    builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    builder.set_multisampling_none();
    builder.disable_blending();
    builder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    VkFormat gFormats[] = {
        _swapchainManager->gBufferPosition().imageFormat, _swapchainManager->gBufferNormal().imageFormat,
        _swapchainManager->gBufferAlbedo().imageFormat
    };
    builder.set_color_attachment_formats(std::span<VkFormat>(gFormats, 3));
    builder.set_depth_format(_swapchainManager->depthImage().imageFormat);
    _gBufferPipeline = builder.build_pipeline(_deviceManager->device());

    VkDescriptorSetLayout lLayouts[] = {_gpuSceneDataDescriptorLayout, _gBufferInputDescriptorLayout};
    VkPipelineLayoutCreateInfo lightLayoutInfo = vkinit::pipeline_layout_create_info();
    lightLayoutInfo.setLayoutCount = 2;
    lightLayoutInfo.pSetLayouts = lLayouts;
    VK_CHECK(vkCreatePipelineLayout(_deviceManager->device(), &lightLayoutInfo, nullptr, &_lightingPipelineLayout));

    builder.clear();
    builder._pipelineLayout = _lightingPipelineLayout;
    builder.set_shaders(fullscreenVert, lightingFrag);
    builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    builder.set_multisampling_none();
    builder.enable_blending_alphablend();
    builder.disable_depthtest();
    builder.set_color_attachment_format(_swapchainManager->drawImage().imageFormat);
    _lightingPipeline = builder.build_pipeline(_deviceManager->device());

    vkDestroyShaderModule(_deviceManager->device(), vertShader, nullptr);
    vkDestroyShaderModule(_deviceManager->device(), gbufferFrag, nullptr);
    vkDestroyShaderModule(_deviceManager->device(), fullscreenVert, nullptr);
    vkDestroyShaderModule(_deviceManager->device(), lightingFrag, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_deviceManager->device(), _gBufferPipelineLayout, nullptr);
        vkDestroyPipeline(_deviceManager->device(), _gBufferPipeline, nullptr);
        vkDestroyPipelineLayout(_deviceManager->device(), _lightingPipelineLayout, nullptr);
        vkDestroyPipeline(_deviceManager->device(), _lightingPipeline, nullptr);
    });
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

#include "vk_renderpass_lighting.h"
#include "core/vk_engine.h"
#include "core/vk_images.h"
#include "core/vk_initializers.h"
#include "core/vk_resource.h"
#include "render/vk_pipelines.h"
#include "core/vk_descriptors.h"

#include "vk_mem_alloc.h"

void LightingPass::init(VulkanEngine *engine)
{
    _engine = engine;

    // Build descriptor layout for GBuffer inputs
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _gBufferInputDescriptorLayout = builder.build(_engine->_deviceManager->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // Allocate and write GBuffer descriptor set
    _gBufferInputDescriptorSet = _engine->globalDescriptorAllocator.allocate(
        _engine->_deviceManager->device(), _gBufferInputDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.write_image(0, _engine->_swapchainManager->gBufferPosition().imageView, _engine->_defaultSamplerLinear,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(1, _engine->_swapchainManager->gBufferNormal().imageView, _engine->_defaultSamplerLinear,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(2, _engine->_swapchainManager->gBufferAlbedo().imageView, _engine->_defaultSamplerLinear,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.update_set(_engine->_deviceManager->device(), _gBufferInputDescriptorSet);
    }

    // Build lighting pipeline
    VkShaderModule fullscreenVert;
    VkShaderModule lightingFrag;
    bool fullscreenLoaded = vkutil::load_shader_module("../shaders/fullscreen.vert.spv", _engine->_deviceManager->device(),
                                                       &fullscreenVert);
    bool lightingLoaded = vkutil::load_shader_module("../shaders/deferred_lighting.frag.spv",
                                                     _engine->_deviceManager->device(), &lightingFrag);
    if (!fullscreenLoaded || !lightingLoaded)
    {
        fmt::println("Failed to load lighting shaders");
        return;
    }

    VkDescriptorSetLayout layouts[] = {_engine->_gpuSceneDataDescriptorLayout, _gBufferInputDescriptorLayout};
    VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = layouts;
    VK_CHECK(vkCreatePipelineLayout(_engine->_deviceManager->device(), &layoutInfo, nullptr, &_pipelineLayout));

    PipelineBuilder builder;
    builder._pipelineLayout = _pipelineLayout;
    builder.set_shaders(fullscreenVert, lightingFrag);
    builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    builder.set_multisampling_none();
    builder.enable_blending_alphablend();
    builder.disable_depthtest();
    builder.set_color_attachment_format(_engine->_swapchainManager->drawImage().imageFormat);
    _pipeline = builder.build_pipeline(_engine->_deviceManager->device());

    vkDestroyShaderModule(_engine->_deviceManager->device(), fullscreenVert, nullptr);
    vkDestroyShaderModule(_engine->_deviceManager->device(), lightingFrag, nullptr);

    _engine->_mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_engine->_deviceManager->device(), _pipelineLayout, nullptr);
        vkDestroyPipeline(_engine->_deviceManager->device(), _pipeline, nullptr);
        vkDestroyDescriptorSetLayout(_engine->_deviceManager->device(), _gBufferInputDescriptorLayout, nullptr);
    });
}

void LightingPass::execute(VkCommandBuffer cmd)
{
    vkutil::transition_image(cmd, _engine->_swapchainManager->gBufferPosition().image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_swapchainManager->gBufferNormal().image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_swapchainManager->gBufferAlbedo().image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_swapchainManager->drawImage().image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    draw_lighting(cmd);
}

void LightingPass::draw_lighting(VkCommandBuffer cmd)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(
        _engine->_swapchainManager->drawImage().imageView, nullptr,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_engine->_drawExtent, &colorAttachment, nullptr);
    vkCmdBeginRendering(cmd, &renderInfo);

    AllocatedBuffer gpuSceneDataBuffer = _engine->_resourceManager->create_buffer(
        sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    _engine->get_current_frame()._deletionQueue.push_function([=, this]() {
        _engine->_resourceManager->destroy_buffer(gpuSceneDataBuffer);
    });

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(_engine->_deviceManager->allocator(), gpuSceneDataBuffer.allocation, &allocInfo);
    auto *sceneUniformData = static_cast<GPUSceneData *>(allocInfo.pMappedData);
    *sceneUniformData = _engine->_sceneManager->getSceneData();

    VkDescriptorSet globalDescriptor = _engine->get_current_frame()._frameDescriptors.allocate(
        _engine->_deviceManager->device(), _engine->_gpuSceneDataDescriptorLayout);
    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_engine->_deviceManager->device(), globalDescriptor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &globalDescriptor, 0,
                            nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 1, 1,
                            &_gBufferInputDescriptorSet, 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(_engine->_drawExtent.width);
    viewport.height = static_cast<float>(_engine->_drawExtent.height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {_engine->_drawExtent.width, _engine->_drawExtent.height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

void LightingPass::cleanup()
{
    fmt::print("LightingPass::cleanup()\n");
}

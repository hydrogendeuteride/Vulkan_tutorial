#include "vk_renderpass_lighting.h"

#include "frame_resources.h"
#include "vk_descriptor_manager.h"
#include "vk_device.h"
#include "core/engine_context.h"
#include "core/vk_images.h"
#include "core/vk_initializers.h"
#include "core/vk_resource.h"
#include "render/vk_pipelines.h"
#include "core/vk_descriptors.h"

#include "vk_mem_alloc.h"
#include "vk_sampler_manager.h"
#include "vk_swapchain.h"

void LightingPass::init(EngineContext *context)
{
    _context = context;

    // Build descriptor layout for GBuffer inputs
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _gBufferInputDescriptorLayout = builder.build(_context->getDevice()->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // Allocate and write GBuffer descriptor set
    _gBufferInputDescriptorSet = _context->getDescriptors()->allocate(
        _context->getDevice()->device(), _gBufferInputDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.write_image(0, _context->getSwapchain()->gBufferPosition().imageView, _context->getSamplers()->defaultLinear(),
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(1, _context->getSwapchain()->gBufferNormal().imageView, _context->getSamplers()->defaultLinear(),
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.write_image(2, _context->getSwapchain()->gBufferAlbedo().imageView, _context->getSamplers()->defaultLinear(),
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.update_set(_context->getDevice()->device(), _gBufferInputDescriptorSet);
    }

    // Build lighting pipeline
    VkShaderModule fullscreenVert;
    VkShaderModule lightingFrag;
    bool fullscreenLoaded = vkutil::load_shader_module("../shaders/fullscreen.vert.spv", _context->getDevice()->device(),
                                                       &fullscreenVert);
    bool lightingLoaded = vkutil::load_shader_module("../shaders/deferred_lighting.frag.spv",
                                                     _context->getDevice()->device(), &lightingFrag);
    if (!fullscreenLoaded || !lightingLoaded)
    {
        fmt::println("Failed to load lighting shaders");
        return;
    }

    VkDescriptorSetLayout layouts[] = {_context->getDescriptorLayouts()->gpuSceneDataLayout(), _gBufferInputDescriptorLayout};
    VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = layouts;
    VK_CHECK(vkCreatePipelineLayout(_context->device->device(), &layoutInfo, nullptr, &_pipelineLayout));

    PipelineBuilder builder;
    builder._pipelineLayout = _pipelineLayout;
    builder.set_shaders(fullscreenVert, lightingFrag);
    builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    builder.set_multisampling_none();
    builder.enable_blending_alphablend();
    builder.disable_depthtest();
    builder.set_color_attachment_format(_context->getSwapchain()->drawImage().imageFormat);
    _pipeline = builder.build_pipeline(_context->getDevice()->device());

    vkDestroyShaderModule(_context->getDevice()->device(), fullscreenVert, nullptr);
    vkDestroyShaderModule(_context->getDevice()->device(), lightingFrag, nullptr);

    _deletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_context->getDevice()->device(), _pipelineLayout, nullptr);
        vkDestroyPipeline(_context->device->device(), _pipeline, nullptr);
        vkDestroyDescriptorSetLayout(_context->getDevice()->device(), _gBufferInputDescriptorLayout, nullptr);
    });
}

void LightingPass::execute(VkCommandBuffer cmd)
{
    vkutil::transition_image(cmd, _context->getSwapchain()->gBufferPosition().image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->gBufferNormal().image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->gBufferAlbedo().image,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->drawImage().image,
                             VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    draw_lighting(cmd);
}

void LightingPass::draw_lighting(VkCommandBuffer cmd)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(
        _context->getSwapchain()->drawImage().imageView, nullptr,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_context->getDrawExtent(), &colorAttachment, nullptr);
    vkCmdBeginRendering(cmd, &renderInfo);

    AllocatedBuffer gpuSceneDataBuffer = _context->getResources()->create_buffer(
        sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    _context->currentFrame->_deletionQueue.push_function([=, this]() {
        _context->getResources()->destroy_buffer(gpuSceneDataBuffer);
    });

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(_context->getDevice()->allocator(), gpuSceneDataBuffer.allocation, &allocInfo);
    auto *sceneUniformData = static_cast<GPUSceneData *>(allocInfo.pMappedData);
    *sceneUniformData = _context->getSceneData();

    VkDescriptorSet globalDescriptor = _context->currentFrame->_frameDescriptors.allocate(
        _context->getDevice()->device(), _context->getDescriptorLayouts()->gpuSceneDataLayout());
    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_context->getDevice()->device(), globalDescriptor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &globalDescriptor, 0,
                            nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 1, 1,
                            &_gBufferInputDescriptorSet, 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(_context->getDrawExtent().width);
    viewport.height = static_cast<float>(_context->getDrawExtent().height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {_context->getDrawExtent().width, _context->getDrawExtent().height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

void LightingPass::cleanup()
{
    _deletionQueue.flush();
    fmt::print("LightingPass::cleanup()\n");
}

#include "vk_materials.h"

#include "core/vk_engine.h"
#include "render/vk_pipelines.h"
#include "core/vk_initializers.h"

namespace vkutil { bool load_shader_module(const char*, VkDevice, VkShaderModule*); }

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
        engine->_descriptorManager->gpuSceneDataLayout(),
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

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertexShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    pipelineBuilder.set_color_attachment_format(engine->_swapchainManager->drawImage().imageFormat);
    pipelineBuilder.set_depth_format(engine->_swapchainManager->depthImage().imageFormat);
    pipelineBuilder._pipelineLayout = newLayout;

    opaquePipeline.pipeline = pipelineBuilder.build_pipeline(engine->_deviceManager->device());

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
        engine->_swapchainManager->gBufferNormal().imageFormat,
        engine->_swapchainManager->gBufferAlbedo().imageFormat
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

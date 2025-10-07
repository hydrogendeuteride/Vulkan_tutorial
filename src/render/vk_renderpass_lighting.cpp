#include "vk_renderpass_lighting.h"

#include "frame_resources.h"
#include "vk_descriptor_manager.h"
#include "vk_device.h"
#include "core/engine_context.h"
#include "core/vk_initializers.h"
#include "core/vk_resource.h"
#include "render/vk_pipelines.h"
#include "core/vk_pipeline_manager.h"
#include "core/asset_manager.h"
#include "core/vk_descriptors.h"

#include "vk_mem_alloc.h"
#include "vk_sampler_manager.h"
#include "vk_swapchain.h"
#include "render/rg_graph.h"

void LightingPass::init(EngineContext *context)
{
    _context = context;

    // Build descriptor layout for GBuffer inputs (set=1)
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _gBufferInputDescriptorLayout = builder.build(_context->getDevice()->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // Build descriptor layout for Shadow inputs (set=2)
    {
        constexpr uint32_t NUM_CASCADES = 4; // must match shader include
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER); // ShadowData UBO
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, NUM_CASCADES); // array
        _shadowSetLayout = builder.build(_context->getDevice()->device(), VK_SHADER_STAGE_FRAGMENT_BIT);
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

    // Build lighting pipeline through PipelineManager
    VkDescriptorSetLayout layouts[] = {
        _context->getDescriptorLayouts()->gpuSceneDataLayout(),
        _gBufferInputDescriptorLayout,
        _shadowSetLayout
    };

    GraphicsPipelineCreateInfo info{};
    info.vertexShaderPath = _context->getAssets()->shaderPath("fullscreen.vert.spv");
    info.fragmentShaderPath = _context->getAssets()->shaderPath("deferred_lighting.frag.spv");
    info.setLayouts.assign(std::begin(layouts), std::end(layouts));
    info.configure = [this](PipelineBuilder &b) {
        b.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        b.set_polygon_mode(VK_POLYGON_MODE_FILL);
        b.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        b.set_multisampling_none();
        b.enable_blending_alphablend();
        b.disable_depthtest();
        b.set_color_attachment_format(_context->getSwapchain()->drawImage().imageFormat);
    };
    _context->pipelines->createGraphicsPipeline("deferred_lighting", info);

    // fetch the handles so current frame uses latest versions
    MaterialPipeline mp{};
    _context->pipelines->getMaterialPipeline("deferred_lighting", mp);
    _pipeline = mp.pipeline;
    _pipelineLayout = mp.layout;

    _deletionQueue.push_function([&]() {
        // Pipelines are owned by PipelineManager; only destroy our local descriptor set layout
        vkDestroyDescriptorSetLayout(_context->getDevice()->device(), _gBufferInputDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_context->getDevice()->device(), _shadowSetLayout, nullptr);
    });
}

void LightingPass::execute(VkCommandBuffer)
{
    // Lighting is executed via the render graph now.
}

void LightingPass::register_graph(RenderGraph *graph,
                                  RGImageHandle drawHandle,
                                  RGImageHandle gbufferPosition,
                                  RGImageHandle gbufferNormal,
                                  RGImageHandle gbufferAlbedo,
                                  const std::vector<RGImageHandle>& shadowMaps,
                                  const std::vector<float>& shadowSplits,
                                  const std::vector<glm::mat4>& shadowLightVP,
                                  uint32_t shadowCascadeCount,
                                  uint32_t shadowMapSize,
                                  float shadowSampleBias,
                                  bool visualizeShadow)
{
    if (!graph || !drawHandle.valid() || !gbufferPosition.valid() || !gbufferNormal.valid() || !gbufferAlbedo.valid())
    {
        return;
    }

    // Copy params for capture
    auto cascades = shadowMaps; // shallow copy of handles
    auto splits = shadowSplits;
    auto lightVP = shadowLightVP;
    uint32_t cascadeCount = shadowCascadeCount ? shadowCascadeCount : static_cast<uint32_t>(cascades.size());
    uint32_t mapSize = shadowMapSize;
    float sampleBias = shadowSampleBias;
    bool visualize = visualizeShadow;

    graph->add_pass(
        "Lighting",
        RGPassType::Graphics,
        [drawHandle, gbufferPosition, gbufferNormal, gbufferAlbedo, cascades](RGPassBuilder &builder, EngineContext *)
        {
            builder.read(gbufferPosition, RGImageUsage::SampledFragment);
            builder.read(gbufferNormal, RGImageUsage::SampledFragment);
            builder.read(gbufferAlbedo, RGImageUsage::SampledFragment);
            for (auto h : cascades) { if (h) builder.read(h, RGImageUsage::SampledFragment); }

            builder.write_color(drawHandle);
        },
        [this, drawHandle, cascades, splits, lightVP, cascadeCount, mapSize, sampleBias, visualize](VkCommandBuffer cmd, const RGPassResources &res, EngineContext *ctx)
        {
            // Stash cascade handles in the context via closure by writing to a thread-local? Not needed.
            // We'll pass them through via member variables soon if required. For now, build shadow set inline.
            // We forward to draw_lighting which will only handle core pipeline + gbuffer; 
            // shadow descriptors are bound here before drawing.

            if (!ctx || !ctx->currentFrame) return;

            // Prepare ShadowData UBO
            struct ShadowGPUData
            {
                glm::mat4 lightVP[4];
                glm::vec4 splits; // x=d1, y=d2, z=d3, w=d4 (far)
                glm::vec4 params; // x=cascadeCount, y=mapSize, z=bias, w=unused
            };

            ShadowGPUData sdata{};
            sdata.params = glm::vec4((float)std::max(1u, cascadeCount), (float)std::max(1u, mapSize), sampleBias, visualize ? 1.0f : 0.0f);
            for (uint32_t i = 0; i < 4; ++i)
            {
                if (i < lightVP.size()) sdata.lightVP[i] = lightVP[i];
                else sdata.lightVP[i] = glm::mat4(1.0f);
            }
            // Map split distances:
            // - 'splits' from CSM has size = cascadeCount + 1 and starts at near (splits[0]).
            // - The shader expects cascade end distances: x=end of cascade0, y=end of cascade1, z=end of cascade2.
            //   We put the camera far (end of last cascade) in .w for convenience.
            sdata.splits = glm::vec4(0.0f);
            if (splits.size() >= 2) sdata.splits.x = (float)splits[1];
            if (splits.size() >= 3) sdata.splits.y = (float)splits[2];
            if (splits.size() >= 4) sdata.splits.z = (float)splits[3];
            if (splits.size() >= 5) sdata.splits.w = (float)splits[4];
            else                    sdata.splits.w = sdata.splits.z; // fallback

            // Allocate UBO
            AllocatedBuffer shadowUBO = ctx->getResources()->create_buffer(sizeof(ShadowGPUData),
                                                                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                                           VMA_MEMORY_USAGE_CPU_TO_GPU);
            ctx->currentFrame->_deletionQueue.push_function([rm=ctx->getResources(), shadowUBO]() { rm->destroy_buffer(shadowUBO); });
            {
                VmaAllocationInfo ai{}; vmaGetAllocationInfo(ctx->getDevice()->allocator(), shadowUBO.allocation, &ai);
                *static_cast<ShadowGPUData*>(ai.pMappedData) = sdata;
                vmaFlushAllocation(ctx->getDevice()->allocator(), shadowUBO.allocation, 0, sizeof(ShadowGPUData));
            }

            VkDescriptorSet shadowSet = ctx->currentFrame->_frameDescriptors.allocate(ctx->getDevice()->device(), _shadowSetLayout);

            // Build image infos for cascades
            std::vector<VkDescriptorImageInfo> infos;
            infos.reserve(cascades.size());
            for (auto h : cascades)
            {
                VkImageView v = res.image_view(h);
                if (v == VK_NULL_HANDLE) continue;
                VkDescriptorImageInfo ii{};
                ii.imageView = v;
                ii.sampler = ctx->getSamplers()->shadowCompareSampler();
                // RenderGraph puts depth images used for sampling into
                // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL (see rg_graph.cpp).
                // Match the descriptor layout to the actual image layout to avoid UB.
                ii.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                infos.push_back(ii);
            }

            DescriptorWriter sw;
            sw.write_buffer(0, shadowUBO.buffer, sizeof(ShadowGPUData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            if (!infos.empty()) sw.write_images(1, infos, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            sw.update_set(ctx->getDevice()->device(), shadowSet);

            // Draw lighting while also binding the shadow descriptor set.
            draw_lighting(cmd, ctx, res, drawHandle, shadowSet);
        });
}

void LightingPass::draw_lighting(VkCommandBuffer cmd,
                                 EngineContext *context,
                                 const RGPassResources &resources,
                                 RGImageHandle drawHandle,
                                 VkDescriptorSet shadowSet)
{
    EngineContext *ctxLocal = context ? context : _context;
    if (!ctxLocal || !ctxLocal->currentFrame) return;

    ResourceManager *resourceManager = ctxLocal->getResources();
    DeviceManager *deviceManager = ctxLocal->getDevice();
    DescriptorManager *descriptorLayouts = ctxLocal->getDescriptorLayouts();
    PipelineManager *pipelineManager = ctxLocal->pipelines;
    if (!resourceManager || !deviceManager || !descriptorLayouts || !pipelineManager) return;

    VkImageView drawView = resources.image_view(drawHandle);
    if (drawView == VK_NULL_HANDLE) return;

    // Re-fetch pipeline in case it was hot-reloaded
    pipelineManager->getGraphics("deferred_lighting", _pipeline, _pipelineLayout);

    // Dynamic rendering is handled by the RenderGraph using the declared draw attachment.

    AllocatedBuffer gpuSceneDataBuffer = resourceManager->create_buffer(
        sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);
    ctxLocal->currentFrame->_deletionQueue.push_function([resourceManager, gpuSceneDataBuffer]()
    {
        resourceManager->destroy_buffer(gpuSceneDataBuffer);
    });

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(deviceManager->allocator(), gpuSceneDataBuffer.allocation, &allocInfo);
    auto *sceneUniformData = static_cast<GPUSceneData *>(allocInfo.pMappedData);
    *sceneUniformData = ctxLocal->getSceneData();
    vmaFlushAllocation(deviceManager->allocator(), gpuSceneDataBuffer.allocation, 0, sizeof(GPUSceneData));

    VkDescriptorSet globalDescriptor = ctxLocal->currentFrame->_frameDescriptors.allocate(
        deviceManager->device(), descriptorLayouts->gpuSceneDataLayout());
    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(deviceManager->device(), globalDescriptor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &globalDescriptor, 0,
                            nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 1, 1,
                            &_gBufferInputDescriptorSet, 0, nullptr);
    if (shadowSet != VK_NULL_HANDLE)
    {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 2, 1, &shadowSet, 0, nullptr);
    }

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(ctxLocal->getDrawExtent().width);
    viewport.height = static_cast<float>(ctxLocal->getDrawExtent().height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {ctxLocal->getDrawExtent().width, ctxLocal->getDrawExtent().height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    // RenderGraph ends rendering.
}

void LightingPass::cleanup()
{
    _deletionQueue.flush();
    fmt::print("LightingPass::cleanup()\n");
}

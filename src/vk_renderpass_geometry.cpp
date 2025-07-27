#include "vk_renderpass_geometry.h"
#include "vk_engine.h"
#include "vk_images.h"
#include "vk_initializers.h"

void GeometryPass::init(VulkanEngine *engine)
{
    _engine = engine;
}

void GeometryPass::execute(VkCommandBuffer cmd)
{
    vkutil::transition_image(cmd, _engine->_gBufferPosition.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_gBufferNormal.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_gBufferAlbedo.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    draw_geometry(cmd);
}

void GeometryPass::draw_geometry(VkCommandBuffer cmd)
{
auto start = std::chrono::system_clock::now();

    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(_engine->mainDrawContext.OpaqueSurfaces.size());

    for (int i = 0; i < _engine->mainDrawContext.OpaqueSurfaces.size(); i++)
    {
        if (is_visible(_engine->mainDrawContext.OpaqueSurfaces[i], _engine->sceneData.viewproj))
        {
            opaque_draws.push_back(i);
        }
    }
    for (int i = 0; i < _engine->mainDrawContext.OpaqueSurfaces.size(); i++)
    {
        opaque_draws.push_back(i);
    }

    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto &iA, const auto &iB) {
        const RenderObject &A = _engine->mainDrawContext.OpaqueSurfaces[iA];
        const RenderObject &B = _engine->mainDrawContext.OpaqueSurfaces[iB];
        if (A.material == B.material)
        {
            return A.indexBuffer < B.indexBuffer;
        }
        return A.material < B.material;
    });

    //begin a render pass  connected to our draw image
    VkRenderingAttachmentInfo gbufferAttachments[3];
    VkClearValue gbufferClear{};
    gbufferClear.color = {{0.f, 0.f, 0.f, 0.f}};
    gbufferAttachments[0] = vkinit::attachment_info( _engine->swapchainManager._gBufferPosition.imageView, &gbufferClear,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    gbufferAttachments[1] = vkinit::attachment_info( _engine->swapchainManager._gBufferNormal.imageView, &gbufferClear,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    gbufferAttachments[2] = vkinit::attachment_info(_gBufferAlbedo.imageView, &gbufferClear,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(
         _engine->swapchainManager._depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info_multi(_drawExtent, 3, gbufferAttachments, &depthAttachment);
    vkCmdBeginRendering(cmd, &renderInfo);

    //allocate a new uniform buffer for the scene data
    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       VMA_MEMORY_USAGE_CPU_TO_GPU);

    //add it to the deletion queue of this frame so it gets deleted once its been used
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
    });

    //write the buffer
    auto *sceneUniformData = static_cast<GPUSceneData *>(gpuSceneDataBuffer.allocation->GetMappedData());
    *sceneUniformData = sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptors.allocate(
        _device, _gpuSceneDataDescriptorLayout);

    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, globalDescriptor);

    MaterialPipeline *lastPipeline = nullptr;
    MaterialInstance *lastMaterial = nullptr;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    auto draw = [&](const RenderObject &r) {
        if (r.material != lastMaterial)
        {
            lastMaterial = r.material;
            if (r.material->pipeline != lastPipeline)
            {
                lastPipeline = r.material->pipeline;
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 0, 1,
                                        &globalDescriptor, 0, nullptr);

                VkViewport viewport = {};
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = static_cast<float>(_windowExtent.width);
                viewport.height = static_cast<float>(_windowExtent.height);
                viewport.minDepth = 0.f;
                viewport.maxDepth = 1.f;

                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = _windowExtent.width;
                scissor.extent.height = _windowExtent.height;

                vkCmdSetScissor(cmd, 0, 1, &scissor);
            }

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1,
                                    &r.material->materialSet, 0, nullptr);
        }
        if (r.indexBuffer != lastIndexBuffer)
        {
            lastIndexBuffer = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }
        GPUDrawPushConstants push_constants{};
        push_constants.worldMatrix = r.transform;
        push_constants.vertexBuffer = r.vertexBufferAddress;

        vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(GPUDrawPushConstants), &push_constants);

        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);

        stats.drawcall_count++;
        stats.triangle_count += r.indexCount / 3;
    };

    stats.drawcall_count = 0;
    stats.triangle_count = 0;

    for (auto &r: opaque_draws)
    {
        draw(mainDrawContext.OpaqueSurfaces[r]);
    }

    for (auto &r: mainDrawContext.TransparentSurfaces)
    {
        draw(r);
    }

    vkCmdEndRendering(cmd);

    auto end = std::chrono::system_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.mesh_draw_time = elapsed.count() / 1000.f;
}
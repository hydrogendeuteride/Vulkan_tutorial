#include "vk_renderpass_geometry.h"

#include <chrono>

#include "frame_resources.h"
#include "vk_descriptor_manager.h"
#include "vk_device.h"
#include "core/engine_context.h"
#include "core/vk_images.h"
#include "core/vk_initializers.h"
#include "core/vk_resource.h"

#include "vk_mem_alloc.h"
#include "vk_scene.h"
#include "vk_swapchain.h"

bool is_visible(const RenderObject &obj, const glm::mat4 &viewproj)
{
    std::array<glm::vec3, 8> corners{
        glm::vec3{1, 1, 1},
        glm::vec3{1, 1, -1},
        glm::vec3{1, -1, 1},
        glm::vec3{1, -1, -1},
        glm::vec3{-1, 1, 1},
        glm::vec3{-1, 1, -1},
        glm::vec3{-1, -1, 1},
        glm::vec3{-1, -1, -1},
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = {2., 2., 2.};
    glm::vec3 max = {-2., -2., -2.};

    for (int c = 0; c < 8; c++)
    {
        // project each corner into clip space
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

        // perspective correction
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(glm::vec3{v.x, v.y, v.z}, min);
        max = glm::max(glm::vec3{v.x, v.y, v.z}, max);
    }

    // check the clip space box is within the view
    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f)
    {
        return false;
    }
    return true;
}

void GeometryPass::init(EngineContext *context)
{
    _context = context;
}

void GeometryPass::execute(VkCommandBuffer cmd)
{
    vkutil::transition_image(cmd, _context->getSwapchain()->gBufferPosition().image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->gBufferNormal().image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->gBufferAlbedo().image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->depthImage().image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    draw_geometry(cmd);
}

void GeometryPass::draw_geometry(VkCommandBuffer cmd) const
{
    const auto& mainDrawContext = _context->getMainDrawContext();
    const auto& sceneData = _context->getSceneData();

    auto start = std::chrono::system_clock::now();

    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(mainDrawContext.OpaqueSurfaces.size());

    for (int i = 0; i < mainDrawContext.OpaqueSurfaces.size(); i++)
    {
        // if (is_visible(mainDrawContext.OpaqueSurfaces[i], sceneData.viewproj))
        // {
        //     opaque_draws.push_back(i);
        // }

        opaque_draws.push_back(i);
    }

    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto &iA, const auto &iB) {
        const RenderObject &A = mainDrawContext.OpaqueSurfaces[iA];
        const RenderObject &B = mainDrawContext.OpaqueSurfaces[iB];
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
    gbufferAttachments[0] = vkinit::attachment_info( _context->getSwapchain()->gBufferPosition().imageView, &gbufferClear,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    gbufferAttachments[1] = vkinit::attachment_info( _context->getSwapchain()->gBufferNormal().imageView, &gbufferClear,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    gbufferAttachments[2] = vkinit::attachment_info(_context->getSwapchain()->gBufferAlbedo().imageView, &gbufferClear,
                                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(
         _context->getSwapchain()->depthImage().imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info_multi(_context->drawExtent, 3, gbufferAttachments, &depthAttachment);
    vkCmdBeginRendering(cmd, &renderInfo);

    //allocate a new uniform buffer for the scene data
    AllocatedBuffer gpuSceneDataBuffer = _context->resources->create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                       VMA_MEMORY_USAGE_CPU_TO_GPU);

    //add it to the deletion queue of this frame so it gets deleted once its been used
    _context->currentFrame->_deletionQueue.push_function([=, this]() {
        _context->resources->destroy_buffer(gpuSceneDataBuffer);
    });

    //write the buffer
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(_context->device->allocator(), gpuSceneDataBuffer.allocation, &allocInfo);
    auto *sceneUniformData = static_cast<GPUSceneData *>(allocInfo.pMappedData);
    *sceneUniformData = sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = _context->currentFrame->_frameDescriptors.allocate(
        _context->device->device(), _context->getDescriptorLayouts()->gpuSceneDataLayout());

    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_context->device->device(), globalDescriptor);

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
                viewport.width = static_cast<float>(_context->getDrawExtent().width);
                viewport.height = static_cast<float>(_context->getDrawExtent().height);
                viewport.minDepth = 0.f;
                viewport.maxDepth = 1.f;

                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor = {};
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                scissor.extent.width = _context->getDrawExtent().width;
                scissor.extent.height = _context->getDrawExtent().height;

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

        _context->stats->drawcall_count++;
        _context->stats->triangle_count += r.indexCount / 3;
    };

    if (_context->stats)
    {
        _context->stats->drawcall_count = 0;
        _context->stats->triangle_count = 0;
    }

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
    if (_context->stats)
    {
        _context->stats->mesh_draw_time = elapsed.count() / 1000.f;
    }
}

void GeometryPass::cleanup()
{
    fmt::print("GeometryPass::cleanup()\n");
}

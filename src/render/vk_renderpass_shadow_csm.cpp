#include "vk_renderpass_shadow_csm.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <cmath>
#include <unordered_set>

#include <core/engine_context.h>
#include <core/vk_device.h>
#include <core/vk_descriptor_manager.h>
#include <core/vk_pipeline_manager.h>
#include <core/vk_resource.h>
#include <core/frame_resources.h>
#include <vk_swapchain.h>
#include <vk_scene.h>
#include <render/rg_graph.h>
#include <render/rg_builder.h>
#include <render/rg_resources.h>
#include <render/vk_pipelines.h>
#include <core/asset_manager.h>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    struct ShadowVP
    {
        glm::mat4 lightViewProj;
    };

    // Build a look-at matrix for a directional light given a target point and
    // a world-space light direction (direction that light travels in).
    inline glm::mat4 make_light_view(const glm::vec3 &target, const glm::vec3 &lightDirWS)
    {
        // Ensure unit-length and choose a stable up vector to avoid roll.
        glm::vec3 dir = glm::normalize(lightDirWS);
        glm::vec3 up = (fabsf(glm::dot(dir, glm::vec3(0, 1, 0))) > 0.9f)
                           ? glm::vec3(0, 0, 1)
                           : glm::vec3(0, 1, 0);
        // Place the eye a bit upwind along the opposite of the light
        // direction. Distance is arbitrary for directional lights; the
        // orthographic projection bounds the volume.
        const float kEyeBackoff = 100.0f;
        return glm::lookAt(target - dir * kEyeBackoff, target, up);
    }
}

void CSMShadowPass::init(EngineContext *context)
{
    _context = context;

    // Set layout for set=1 (per-cascade VP matrix)
    {
        DescriptorLayoutBuilder b;
        b.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _vpSetLayout = b.build(_context->getDevice()->device(), VK_SHADER_STAGE_VERTEX_BIT);
    }

    // Build depth-only pipeline through PipelineManager
    VkDescriptorSetLayout setLayouts[] = {
        _context->getDescriptorLayouts()->gpuSceneDataLayout(), // set 0
        _vpSetLayout // set 1
    };

    GraphicsPipelineCreateInfo info{};
    info.vertexShaderPath = _context->getAssets()->shaderPath("shadow_csm.vert.spv");
    info.fragmentShaderPath = _context->getAssets()->shaderPath("shadow_null.frag.spv");
    info.setLayouts.assign(std::begin(setLayouts), std::end(setLayouts)); {
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(GPUDrawPushConstants);
        info.pushConstants = {pcr};
    }
    info.configure = [this](PipelineBuilder &b) {
        b.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        b.set_polygon_mode(VK_POLYGON_MODE_FILL);
        // Light-space rendering does NOT flip Y like the camera path does.
        // Use CCW as front face so geometry is not culled away in the shadow map.
        b.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        b.set_multisampling_none();
        b.disable_blending();
        b.enable_depthtest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
        b.set_depth_format(VK_FORMAT_D32_SFLOAT);
        // Depth bias for shadow acne mitigation
        b._rasterizer.depthBiasEnable = VK_TRUE;
        b._rasterizer.depthBiasConstantFactor = _cfg.depthBiasConstant;
        b._rasterizer.depthBiasSlopeFactor = _cfg.depthBiasSlope;
    };

    _context->pipelines->createGraphicsPipeline("shadow_csm", info);

    // Cache handles for immediate usage
    MaterialPipeline mp{};
    _context->pipelines->getMaterialPipeline("shadow_csm", mp);
    _pipeline = mp.pipeline;
    _pipelineLayout = mp.layout;

    _lightViewProj.resize(_cfg.cascades, glm::mat4(1.f));

    _deletionQueue.push_function([this]() {
        if (_context)
        {
            VkDevice device = _context->getDevice()->device();
            if (_vpSetLayout)
            {
                vkDestroyDescriptorSetLayout(device, _vpSetLayout, nullptr);
                _vpSetLayout = VK_NULL_HANDLE;
            }
        }
    });
}

void CSMShadowPass::cleanup()
{
    _deletionQueue.flush();
}

void CSMShadowPass::execute(VkCommandBuffer)
{
    // Executed via render graph
}

void CSMShadowPass::register_graph(RenderGraph *graph)
{
    if (!graph || !_context) return;

    _shadowImages.clear();
    _shadowImages.reserve(_cfg.cascades);

    // Compute per-cascade matrices and splits up front for this frame
    _splits = compute_splits();
    if (_lightViewProj.size() != _cfg.cascades) _lightViewProj.resize(_cfg.cascades, glm::mat4(1.0f));
    for (uint32_t i = 0; i < _cfg.cascades; ++i) update_cascade_matrix(i);

    for (uint32_t i = 0; i < _cfg.cascades; ++i)
    {
        RGImageDesc d{};
        d.name = std::string("shadow.cascade.") + std::to_string(i);
        d.format = VK_FORMAT_D32_SFLOAT;
        d.extent = VkExtent2D{_cfg.mapSize, _cfg.mapSize};
        d.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        RGImageHandle img = graph->create_image(d);
        _shadowImages.push_back(img);
    }

    // One graphics pass per cascade
    for (uint32_t i = 0; i < _cfg.cascades; ++i)
    {
        const RGImageHandle target = _shadowImages[i];
        graph->add_pass(
            (std::string("ShadowCascade ") + std::to_string(i)).c_str(),
            RGPassType::Graphics,
            [target](RGPassBuilder &builder, EngineContext *ctx) {
                // Clear depth to 1.0 (far) for shadow map
                VkClearValue depthClear{};
                depthClear.depthStencil = {1.0f, 0};
                builder.write_depth(target, true, depthClear);

                // Track geometry buffers as reads for proper sync
                if (ctx)
                {
                    const DrawContext &dc = ctx->getMainDrawContext();
                    std::unordered_set<VkBuffer> indexSet;
                    std::unordered_set<VkBuffer> vertexSet;
                    auto collect = [&](const std::vector<RenderObject> &v) {
                        for (const auto &r: v)
                        {
                            if (r.indexBuffer) indexSet.insert(r.indexBuffer);
                            if (r.vertexBuffer) vertexSet.insert(r.vertexBuffer);
                        }
                    };
                    collect(dc.OpaqueSurfaces);
                    for (VkBuffer b: indexSet) builder.read_buffer(b, RGBufferUsage::IndexRead, 0, "csm.index");
                    for (VkBuffer b: vertexSet) builder.read_buffer(b, RGBufferUsage::StorageRead, 0, "csm.vertex");
                }
            },
            [this, i](VkCommandBuffer cmd, const RGPassResources &, EngineContext *ctx) {
                draw_cascade(cmd, ctx ? ctx : _context, i);
            }
        );
    }
}

std::vector<float> CSMShadowPass::compute_splits() const
{
    // Practical split scheme between near and maxDistance (camera near ~= 0.1)
    const float n = 0.1f;
    const float f = _cfg.maxDistance;
    std::vector<float> splits(_cfg.cascades + 1);
    splits[0] = n;
    for (uint32_t i = 1; i < _cfg.cascades; ++i)
    {
        float si = (float) i / (float) _cfg.cascades;
        float logd = n * std::pow(f / n, si);
        float unid = n + (f - n) * si;
        splits[i] = glm::mix(unid, logd, _cfg.splitLambda);
    }
    splits[_cfg.cascades] = f;
    return splits;
}

void CSMShadowPass::update_cascade_matrix(uint32_t cascadeIndex)
{
    if (!_context) return;
    const auto &sd = _context->getSceneData();
    const glm::mat4 invView = glm::inverse(sd.view);

    // Camera parameters
    const float fov = glm::radians(_context->scene->getMainCamera().fovDegrees);
    const float aspect = (float) _context->getSwapchain()->windowExtent().width /
                         (float) _context->getSwapchain()->windowExtent().height;

    const auto splits = compute_splits();
    const float n = splits[cascadeIndex];
    const float f = splits[cascadeIndex + 1];

    // Build camera basis in world space from inverse view
    const glm::vec3 camPos = glm::vec3(invView[3]);
    const glm::vec3 camRight = glm::normalize(glm::vec3(invView * glm::vec4(1, 0, 0, 0)));
    const glm::vec3 camUp = glm::normalize(glm::vec3(invView * glm::vec4(0, 1, 0, 0)));
    // Forward is -Z in view space; transform to world
    const glm::vec3 camFwd = glm::normalize(glm::vec3(invView * glm::vec4(0, 0, -1, 0)));

    const float tanHalf = tanf(fov * 0.5f);
    const float nh = n * tanHalf;
    const float nw = nh * aspect;
    const float fh = f * tanHalf;
    const float fw = fh * aspect;

    // Centers of the near/far planes in world space
    const glm::vec3 cNear = camPos + camFwd * n;
    const glm::vec3 cFar = camPos + camFwd * f;

    // 8 frustum corners in world space
    std::array<glm::vec3, 8> cornersWS{
        cNear + (-camRight * nw) + (camUp * nh),
        cNear + (camRight * nw) + (camUp * nh),
        cNear + (camRight * nw) + (-camUp * nh),
        cNear + (-camRight * nw) + (-camUp * nh),
        cFar + (-camRight * fw) + (camUp * fh),
        cFar + (camRight * fw) + (camUp * fh),
        cFar + (camRight * fw) + (-camUp * fh),
        cFar + (-camRight * fw) + (-camUp * fh)
    };

    // Light direction in world space: direction that light travels in.
    // make_light_view() expects this exact convention.
    glm::vec3 lightDir = glm::normalize(glm::vec3(sd.sunlightDirection));

    // Frustum center
    glm::vec3 center(0.f);
    for (const auto &p: cornersWS) center += p;
    center *= (1.f / 8.f);

    // Build initial light view (eye placed well upwind along light direction)
    glm::mat4 lightView = make_light_view(center, lightDir);

    // Project corners to light space and build AABB for Z range
    glm::vec3 minLS(FLT_MAX), maxLS(-FLT_MAX);
    for (const auto &p: cornersWS)
    {
        glm::vec3 ls = glm::vec3(lightView * glm::vec4(p, 1.f));
        minLS = glm::min(minLS, ls);
        maxLS = glm::max(maxLS, ls);
    }

    // Compute a FIXED per‑cascade radius from the frustum slice itself, not from the
    // light-space AABB. This keeps texel size constant as the camera moves/rotates.
    // Use the max distance from slice center to any corner in WORLD space; rotation to
    // light space preserves length, so this safely bounds XY in light space.
    float fixedRadius = 1.0f;
    for (const auto &p: cornersWS)
    {
        fixedRadius = std::max(fixedRadius, glm::length(p - center));
    }

    // Stabilize by snapping the LIGHT-SPACE center (derived from world center) to the
    // shadow-map texel grid using the fixed radius.
    glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.f));
    float texelWorld = (2.0f * fixedRadius) / float(std::max(1u, _cfg.mapSize));
    if (texelWorld > 0.0f)
    {
        centerLS.x = std::floor(centerLS.x / texelWorld + 0.5f) * texelWorld;
        centerLS.y = std::floor(centerLS.y / texelWorld + 0.5f) * texelWorld;
    }

    // Build orthographic box centered on (snapped) light-space center, with a square extent
    // derived from the FIXED radius. This prevents per-frame window size changes.
    float left = centerLS.x - fixedRadius;
    float right = centerLS.x + fixedRadius;
    float bottom = centerLS.y - fixedRadius;
    float top = centerLS.y + fixedRadius;

    // Depth range: use the original light-space bounds and pad slightly.
    const float zPad = 50.0f;
    float nearDist = std::max(0.0f, (-maxLS.z) - zPad); // bring near closer to the light
    float farDist = std::max(nearDist + 1.0f, (-minLS.z) + zPad); // ensure far > near

    // Ortho projection in light space (GLM with ZO maps z to 0..1)
    glm::mat4 lightProj = glm::ortho(left, right, bottom, top, nearDist, farDist);

    _lightViewProj[cascadeIndex] = lightProj * lightView;
}

void CSMShadowPass::draw_cascade(VkCommandBuffer cmd,
                                 EngineContext *ctx,
                                 uint32_t cascadeIndex)
{
    if (!ctx || !ctx->currentFrame) return;

    // Matrices are computed once during graph registration to keep the light
    // matrices used for sampling identical to those used for rendering.
    // Avoid recomputing here to prevent camera-coupled drift.

    ResourceManager *resources = ctx->getResources();
    DeviceManager *device = ctx->getDevice();
    DescriptorManager *descLayouts = ctx->getDescriptorLayouts();
    if (!resources || !device || !descLayouts) return;

    // Scene UBO (set 0)
    AllocatedBuffer sceneUBO = resources->create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                        VMA_MEMORY_USAGE_CPU_TO_GPU);
    ctx->currentFrame->_deletionQueue.push_function([resources, sceneUBO]() { resources->destroy_buffer(sceneUBO); }); {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(device->allocator(), sceneUBO.allocation, &info);
        *static_cast<GPUSceneData *>(info.pMappedData) = ctx->getSceneData();
        vmaFlushAllocation(device->allocator(), sceneUBO.allocation, 0, sizeof(GPUSceneData));
    }
    VkDescriptorSet set0 = ctx->currentFrame->_frameDescriptors.allocate(device->device(),
                                                                         descLayouts->gpuSceneDataLayout()); {
        DescriptorWriter w;
        w.write_buffer(0, sceneUBO.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        w.update_set(device->device(), set0);
    }

    // Cascade VP UBO (set 1)
    ShadowVP vp{};
    vp.lightViewProj = _lightViewProj[cascadeIndex];
    AllocatedBuffer vpUBO = resources->create_buffer(sizeof(ShadowVP), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                     VMA_MEMORY_USAGE_CPU_TO_GPU);
    ctx->currentFrame->_deletionQueue.push_function([resources, vpUBO]() { resources->destroy_buffer(vpUBO); }); {
        VmaAllocationInfo info{};
        vmaGetAllocationInfo(device->allocator(), vpUBO.allocation, &info);
        *static_cast<ShadowVP *>(info.pMappedData) = vp;
        vmaFlushAllocation(device->allocator(), vpUBO.allocation, 0, sizeof(ShadowVP));
    }
    VkDescriptorSet set1 = ctx->currentFrame->_frameDescriptors.allocate(device->device(), _vpSetLayout); {
        DescriptorWriter w;
        w.write_buffer(0, vpUBO.buffer, sizeof(ShadowVP), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        w.update_set(device->device(), set1);
    }

    // Fetch pipeline (hot-reload aware)
    ctx->pipelines->getGraphics("shadow_csm", _pipeline, _pipelineLayout);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &set0, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 1, 1, &set1, 0, nullptr);

    // Viewport/scissor to shadow map size
    VkViewport vpVk{0.f, 0.f, (float) _cfg.mapSize, (float) _cfg.mapSize, 0.f, 1.f};
    VkRect2D sc{{0, 0}, VkExtent2D{_cfg.mapSize, _cfg.mapSize}};
    vkCmdSetViewport(cmd, 0, 1, &vpVk);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // Draw all opaque surfaces (basic; culling can be added later)
    const DrawContext &dc = ctx->getMainDrawContext();

    VkBuffer lastIndex = VK_NULL_HANDLE;
    for (const RenderObject &r: dc.OpaqueSurfaces)
    {
        if (r.indexBuffer != lastIndex)
        {
            lastIndex = r.indexBuffer;
            vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        GPUDrawPushConstants pc{};
        pc.worldMatrix = r.transform;
        pc.vertexBuffer = r.vertexBufferAddress;
        vkCmdPushConstants(cmd, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pc);
        vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
        if (ctx->stats)
        {
            ctx->stats->drawcall_count++;
            ctx->stats->triangle_count += r.indexCount / 3;
        }
    }
}

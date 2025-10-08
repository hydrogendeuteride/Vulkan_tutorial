#pragma once

#include <core/vk_types.h>
#include <render/vk_renderpass.h>
#include <render/rg_types.h>

#include <vector>
#include <array>

class EngineContext;
class RenderGraph;
class RGPassResources;

class CSMShadowPass : public IRenderPass
{
public:
    void init(EngineContext *context) override;

    void cleanup() override;

    void execute(VkCommandBuffer) override; // Executed via render graph
    const char *getName() const override { return "CSMShadows"; }

    // Register all cascade passes and create depth targets
    void register_graph(RenderGraph *graph);

    // Accessors (for lighting integration in a later step)
    const std::vector<RGImageHandle> &shadow_images() const { return _shadowImages; }
    const std::vector<float> &splits() const { return _splits; }
    const std::vector<glm::mat4> &light_vp() const { return _lightViewProj; }
    uint32_t cascade_count() const { return _cfg.cascades; }
    uint32_t map_size() const { return _cfg.mapSize; }

    // Debug/config accessors
    struct Config
    {
        // Default to a single cascade for simple shadow mapping
        uint32_t cascades = 1;
        uint32_t mapSize = 2048;
        float maxDistance = 150.0f; // meters
        float splitLambda = 0.6f; // 0=uniform, 1=log
        float depthBiasConstant = 1.5f; // tuned later per scene scale
        float depthBiasSlope = 1.0f;
        float sampleBias = 0.0015f; // bias used on sampling compare
        bool visualize = false; // visualize shadow term in lighting
    };

    Config &config() { return _cfg; }
    const Config &config() const { return _cfg; }
    void set_cascade_count(uint32_t c) { _cfg.cascades = std::max(1u, std::min(4u, c)); }
    void set_map_size(uint32_t s) { _cfg.mapSize = std::max(256u, s); }

private:
    Config _cfg;

    EngineContext *_context = nullptr;

    // Pipeline for depth-only draw
    VkDescriptorSetLayout _vpSetLayout = VK_NULL_HANDLE; // set=1: mat4 lightVP
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _pipeline = VK_NULL_HANDLE;

    std::vector<RGImageHandle> _shadowImages; // per-cascade depth images
    std::vector<glm::mat4> _lightViewProj; // per-cascade matrices (updated per-frame)
    std::vector<float> _splits; // split distances (view-space z>0)

    // Helpers
    void draw_cascade(VkCommandBuffer cmd,
                      EngineContext *ctx,
                      uint32_t cascadeIndex);

    void update_cascade_matrix(uint32_t cascadeIndex);

    std::vector<float> compute_splits() const;

    DeletionQueue _deletionQueue;
};

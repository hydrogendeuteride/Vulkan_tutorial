#pragma once
#include "vk_renderpass.h"
#include <render/rg_types.h>

class LightingPass : public IRenderPass
{
public:
    void init(EngineContext *context) override;

    void cleanup() override;

    void execute(VkCommandBuffer cmd) override;

    const char *getName() const override { return "Lighting"; }

    void register_graph(class RenderGraph *graph,
                        RGImageHandle drawHandle,
                        RGImageHandle gbufferPosition,
                        RGImageHandle gbufferNormal,
                        RGImageHandle gbufferAlbedo);

private:
    EngineContext *_context = nullptr;

    VkDescriptorSetLayout _gBufferInputDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet _gBufferInputDescriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _pipeline = VK_NULL_HANDLE;

    void draw_lighting(VkCommandBuffer cmd,
                       EngineContext *context,
                       const class RGPassResources &resources,
                       RGImageHandle drawHandle);

    DeletionQueue _deletionQueue;
};

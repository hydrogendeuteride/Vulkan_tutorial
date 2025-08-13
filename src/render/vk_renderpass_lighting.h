#pragma once
#include "vk_renderpass.h"

class LightingPass : public IRenderPass
{
public:
    void init(EngineContext *context) override;

    void cleanup() override;

    void execute(VkCommandBuffer cmd) override;

    const char *getName() const override { return "Lighting"; }

private:
    EngineContext *_context = nullptr;

    VkDescriptorSetLayout _gBufferInputDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet _gBufferInputDescriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _pipeline = VK_NULL_HANDLE;

    void draw_lighting(VkCommandBuffer cmd);

    DeletionQueue _deletionQueue;
};

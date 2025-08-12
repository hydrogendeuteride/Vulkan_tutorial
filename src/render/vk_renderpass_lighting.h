#pragma once
#include "vk_renderpass.h"

class LightingPass : public IRenderPass
{
public:
    void init(VulkanEngine *engine) override;

    void cleanup() override;

    void execute(VkCommandBuffer cmd) override;

    const char *getName() const override { return "Lighting"; }

private:
    VulkanEngine *_engine = nullptr;

    VkDescriptorSetLayout _gBufferInputDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSet _gBufferInputDescriptorSet = VK_NULL_HANDLE;

    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    VkPipeline _pipeline = VK_NULL_HANDLE;

    void draw_lighting(VkCommandBuffer cmd);
};

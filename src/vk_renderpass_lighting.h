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

    void draw_lighting(VkCommandBuffer cmd);
};

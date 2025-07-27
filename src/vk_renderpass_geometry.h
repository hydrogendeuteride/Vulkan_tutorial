#pragma once
#include "vk_renderpass.h"

class SwapchainManager;

class GeometryPass : public IRenderPass
{
public:
    void init(VulkanEngine *engine) override;
    void cleanup() override;
    void execute(VkCommandBuffer cmd) override;

    const char *getName() const override { return "Geometry"; }

private:
    VulkanEngine *_engine = nullptr;

    void draw_geometry(VkCommandBuffer cmd);
};

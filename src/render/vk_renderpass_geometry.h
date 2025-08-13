#pragma once
#include "vk_renderpass.h"

class SwapchainManager;

class GeometryPass : public IRenderPass
{
public:
    void init(EngineContext *context) override;
    void cleanup() override;
    void execute(VkCommandBuffer cmd) override;

    const char *getName() const override { return "Geometry"; }

private:
    EngineContext *_context = nullptr;

    void draw_geometry(VkCommandBuffer cmd) const;
};

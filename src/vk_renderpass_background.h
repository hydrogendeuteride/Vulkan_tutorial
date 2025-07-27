#pragma once
#include "vk_renderpass.h"
#include "vk_compute.h"

struct ComputeEffect;

class BackgroundPass : public IRenderPass
{
public:
    void init(VulkanEngine *engine) override;
    void cleanup() override;
    void execute(VkCommandBuffer cmd) override;
    const char *getName() const override { return "Background"; }

    void setCurrentEffect(int index) { _currentEffect = index; }
    std::vector<ComputeEffect> &getEffects() { return _backgroundEffects; }

private:
    VulkanEngine *_engine = nullptr;
    std::vector<ComputeEffect> _backgroundEffects;
    int _currentEffect = 0;

    void init_background_pipelines();
};

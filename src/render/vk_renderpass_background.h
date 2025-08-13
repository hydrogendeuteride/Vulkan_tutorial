#pragma once
#include "vk_renderpass.h"
#include "compute/vk_compute.h"

class BackgroundPass : public IRenderPass
{
public:
    void init(EngineContext *context) override;
    void cleanup() override;
    void execute(VkCommandBuffer cmd) override;
    const char *getName() const override { return "Background"; }

    void setCurrentEffect(int index) { _currentEffect = index; }
    std::vector<ComputeEffect> &getEffects() { return _backgroundEffects; }

    std::vector<ComputeEffect> _backgroundEffects;
    int _currentEffect = 0;

private:
    EngineContext *_context = nullptr;

    void init_background_pipelines();
};

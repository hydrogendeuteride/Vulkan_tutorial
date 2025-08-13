#pragma once
#include "vk_renderpass.h"
#include "core/vk_types.h"

class ImGuiPass : public IRenderPass
{
public:
    void init(EngineContext *context) override;

    void cleanup() override;

    void execute(VkCommandBuffer cmd) override;

    void executeWithTarget(VkCommandBuffer cmd, VkImageView targetImageView) const;

    const char *getName() const override { return "ImGui"; }

private:
    EngineContext *_context = nullptr;

    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView) const;

    DeletionQueue _deletionQueue;
};

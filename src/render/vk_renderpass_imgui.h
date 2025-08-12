#pragma once
#include "core/vk_engine.h"

class ImGuiPass : public IRenderPass
{
public:
    void init(VulkanEngine *engine) override;

    void cleanup() override;

    void execute(VkCommandBuffer cmd) override;

    void executeWithTarget(VkCommandBuffer cmd, VkImageView targetImageView) const;

    const char *getName() const override { return "ImGui"; }

private:
    VulkanEngine *_engine = nullptr;

    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView) const;

    DeletionQueue _deletionQueue;
};

#pragma once
#include "vk_engine.h"

class ImGuiPass : public IRenderPass
{
public:
    void init(VulkanEngine *engine);

    void cleanup();

    void execute(VkCommandBuffer cmd);

    const char *getName() const { return "ImGui"; }

private:
    VulkanEngine *_engine = nullptr;

    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView) const;

    DeletionQueue _deletionQueue;
};

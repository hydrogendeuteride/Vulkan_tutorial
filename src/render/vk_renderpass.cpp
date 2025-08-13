#include "vk_renderpass.h"

#include "vk_renderpass_background.h"
#include "vk_renderpass_geometry.h"
#include "vk_renderpass_imgui.h"
#include "vk_renderpass_lighting.h"

void RenderPassManager::init(EngineContext *context)
{
    _context = context;

    auto backgroundPass = std::make_unique<BackgroundPass>();
    backgroundPass->init(context);
    addPass(std::move(backgroundPass));

    auto geometryPass = std::make_unique<GeometryPass>();
    geometryPass->init(context);
    addPass(std::move(geometryPass));

    auto lightingPass = std::make_unique<LightingPass>();
    lightingPass->init(context);
    addPass(std::move(lightingPass));
}

void RenderPassManager::cleanup()
{
    for (auto &pass: _passes)
    {
        pass->cleanup();
    }
    fmt::print("RenderPassManager::cleanup()\n");
    _passes.clear();
}

void RenderPassManager::addPass(std::unique_ptr<IRenderPass> pass)
{
    _passes.push_back(std::move(pass));
}

void RenderPassManager::executeAll(VkCommandBuffer cmd) const
{
    for (auto &pass: _passes)
    {
        pass->execute(cmd);
    }
}

void RenderPassManager::setImGuiPass(std::unique_ptr<IRenderPass> imguiPass)
{
    _imguiPass = std::move(imguiPass);
    _imguiPass->init(_context);
}

void RenderPassManager::executeImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    if (_imguiPass)
    {
        if (auto *imgui = dynamic_cast<ImGuiPass *>(_imguiPass.get()))
        {
            imgui->executeWithTarget(cmd, targetImageView);
        }
    }
}

#include "vk_renderpass.h"

#include "vk_renderpass_background.h"
#include "vk_renderpass_geometry.h"
#include "vk_renderpass_lighting.h"

void RenderPassManager::init(VulkanEngine *engine)
{
    _engine = engine;

    auto backgroundPass = std::make_unique<BackgroundPass>();
    backgroundPass->init(engine);
    addPass(std::move(backgroundPass));

    auto geometryPass = std::make_unique<GeometryPass>();
    geometryPass->init(engine);
    addPass(std::move(geometryPass));

    auto lightingPass = std::make_unique<LightingPass>();
    lightingPass->init(engine);
    addPass(std::move(lightingPass));
}

void RenderPassManager::cleanup()
{
    for (auto &pass: _passes)
    {
        pass->cleanup();
    }
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

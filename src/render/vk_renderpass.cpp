#include "vk_renderpass.h"

#include "vk_renderpass_background.h"
#include "vk_renderpass_geometry.h"
#include "vk_renderpass_imgui.h"
#include "vk_renderpass_lighting.h"
#include "vk_renderpass_shadow_csm.h"
#include "vk_renderpass_transparent.h"
#include "vk_renderpass_tonemap.h"

void RenderPassManager::init(EngineContext *context)
{
    _context = context;

    auto backgroundPass = std::make_unique<BackgroundPass>();
    backgroundPass->init(context);
    addPass(std::move(backgroundPass));

    auto geometryPass = std::make_unique<GeometryPass>();
    geometryPass->init(context);
    addPass(std::move(geometryPass));

    // Shadow mapping before lighting
    auto csmPass = std::make_unique<CSMShadowPass>();
    csmPass->init(context);
    addPass(std::move(csmPass));

    auto lightingPass = std::make_unique<LightingPass>();
    lightingPass->init(context);
    addPass(std::move(lightingPass));

    auto transparentPass = std::make_unique<TransparentPass>();
    transparentPass->init(context);
    addPass(std::move(transparentPass));

    auto tonemapPass = std::make_unique<TonemapPass>();
    tonemapPass->init(context);
    addPass(std::move(tonemapPass));
}

void RenderPassManager::cleanup()
{
    for (auto &pass: _passes)
    {
        pass->cleanup();
    }
    if (_imguiPass)
    {
        _imguiPass->cleanup();
    }
    fmt::print("RenderPassManager::cleanup()\n");
    _passes.clear();
    _imguiPass.reset();
}

void RenderPassManager::addPass(std::unique_ptr<IRenderPass> pass)
{
    _passes.push_back(std::move(pass));
}

void RenderPassManager::setImGuiPass(std::unique_ptr<IRenderPass> imguiPass)
{
    _imguiPass = std::move(imguiPass);
    if (_imguiPass)
    {
        _imguiPass->init(_context);
    }
}

ImGuiPass *RenderPassManager::getImGuiPass()
{
    if (!_imguiPass) return nullptr;
    return dynamic_cast<ImGuiPass *>(_imguiPass.get());
}

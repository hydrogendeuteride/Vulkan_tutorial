#include "vk_renderpass_manager.h"


void RenderPassManager::executeAll(VkCommandBuffer cmd, VkExtent2D extent,
                                   SceneManager *scene, ResourceManager *resources)
{
    drawContext.OpaqueSurfaces.clear();
    drawContext.TransparentSurfaces.clear();
    scene->collectDrawCommands(drawContext, resources);

    RenderContext ctx{};
    ctx.cmd = cmd;
    ctx.extent = extent;
    ctx.scene = scene;
    ctx.resources = resources;
    ctx.sceneData = &scene->getSceneData();

    for (auto &pass: passes)
    {
        if (pass->isEnabled())
        {
            pass->execute(ctx);
        }
    }
}
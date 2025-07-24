#include "vk_scene_manager.h"

void RenderObject::collectDrawCommands(DrawContext &ctx, ResourceManager *resources)
{
    if (!visible) return;

    auto *meshResource = resources->getMesh(mesh);
    auto *materialResource = resources->getMaterial(material);
    if (!meshResource || !materialResource) return;

    for (const auto &surface: meshResource->surfaces)
    {
        ::RenderObject cmd{};
        cmd.indexCount = surface.count;
        cmd.firstIndex = surface.startIndex;
        cmd.indexBuffer = meshResource->buffers.indexBuffer.buffer;
        cmd.vertexBufferAddress = meshResource->buffers.vertexBufferAddress;
        cmd.material = &materialResource->data;
        cmd.bounds = surface.bounds;
        cmd.transform = transform;

        if (materialResource->data.passType == MaterialPass::Transparent)
        {
            ctx.TransparentSurfaces.push_back(cmd);
        }
        else
        {
            ctx.OpaqueSurfaces.push_back(cmd);
        }
    }
}

void SceneManager::collectDrawCommands(DrawContext &ctx, ResourceManager *resources)
{
    for (auto &root: sceneGraphRoots)
    {
        root->Draw(glm::mat4{1.0f}, ctx);
    }

    for (auto &obj: renderObjects)
    {
        obj->collectDrawCommands(ctx, resources);
    }
}
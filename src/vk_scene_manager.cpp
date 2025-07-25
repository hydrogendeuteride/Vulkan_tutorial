#include "vk_scene_manager.h"
#include <algorithm>

void SceneManager::init()
{
    sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    sceneData.sunlightDirection = glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f);
    sceneData.sunlightColor = glm::vec4(1.0f, 1.0f, 1.0f, 3.0f);
}

void SceneManager::cleanup()
{
    renderObjects.clear();
    sceneGraphRoots.clear();
    activeCamera = nullptr;
}

RenderObject *SceneManager::createRenderObject()
{
    auto obj = std::make_unique<RenderObject>();
    RenderObject *ptr = obj.get();
    renderObjects.push_back(std::move(obj));
    return ptr;
}

void SceneManager::removeRenderObject(RenderObject *obj)
{
    renderObjects.erase(std::remove_if(renderObjects.begin(), renderObjects.end(),
                                       [obj](const std::unique_ptr<RenderObject> &o) { return o.get() == obj; }),
                        renderObjects.end());
}

void SceneManager::update(float deltaTime)
{
    RenderObject::UpdateContext ctx{deltaTime, this};
    for (auto &o : renderObjects)
    {
        o->update(ctx);
    }

    for (auto &root : sceneGraphRoots)
    {
        root->refreshTransform(glm::mat4{1.0f});
    }
}

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

void SceneManager::setSunlight(const glm::vec3 &direction, const glm::vec3 &color, float intensity)
{
    sceneData.sunlightDirection = glm::vec4(direction, intensity);
    sceneData.sunlightColor = glm::vec4(color, 1.0f);
}

void SceneManager::setAmbientLight(const glm::vec3 &color)
{
    sceneData.ambientColor = glm::vec4(color, 1.0f);
}

void SceneManager::updateSceneData(const RenderView &view)
{
    sceneData.view = view.viewMatrix;
    sceneData.proj = view.projectionMatrix;
    sceneData.viewproj = view.viewProjectionMatrix;
}

#pragma once
#include <core/vk_types.h>
#include <scene/camera.h>
#include <unordered_map>
#include <memory>

#include "scene/vk_loader.h"
class EngineContext;

struct RenderObject
{
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance *material;
    Bounds bounds;

    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct DrawContext
{
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

class SceneManager
{
public:
    void init(EngineContext *context);

    void cleanup();

    void update_scene();

    Camera &getMainCamera() { return mainCamera; }
    const GPUSceneData &getSceneData() const { return sceneData; }
    DrawContext &getMainDrawContext() { return mainDrawContext; }

    void loadScene(const std::string &name, std::shared_ptr<LoadedGLTF> scene);

    std::shared_ptr<LoadedGLTF> getScene(const std::string &name);

    struct SceneStats
    {
        float scene_update_time = 0.f;
    } stats;

private:
    EngineContext *_context = nullptr;

    Camera mainCamera = {};
    GPUSceneData sceneData = {};
    DrawContext mainDrawContext;

    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF> > loadedScenes;
    std::unordered_map<std::string, std::shared_ptr<Node> > loadedNodes;
};

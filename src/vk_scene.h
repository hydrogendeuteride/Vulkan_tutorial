#pragma once
#include <vk_types.h>
#include <camera.h>
#include <unordered_map>
#include <memory>

#include "vk_loader.h"

class VulkanEngine;

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
    void init(VulkanEngine *engine);

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
    VulkanEngine *_engine = nullptr;

    Camera mainCamera = {};
    GPUSceneData sceneData = {};
    DrawContext mainDrawContext;

    std::unordered_map<std::string, std::shared_ptr<LoadedGLTF> > loadedScenes;
    std::unordered_map<std::string, std::shared_ptr<Node> > loadedNodes;
};

// vk_scene_manager.h
#pragma once
#include "camera.h"
#include "vk_loader.h"
#include "vk_render_view.h"
#include "vk_types.h"
#include "vk_resource_manager.h"

class SceneManager;

class RenderObject
{
public:
    uint32_t indexCount{};
    uint32_t firstIndex{};
    VkBuffer indexBuffer{};

    MaterialInstance *material{};
    Bounds bounds{};

    glm::mat4 transform{1.0f};
    VkDeviceAddress vertexBufferAddress{};

    Handle<MeshResource> mesh{};
    bool visible = true;

    //component system
    struct UpdateContext
    {
        float deltaTime;
        SceneManager *scene;
    };

    virtual void update(const UpdateContext &ctx)
    {
    }

    virtual void collectDrawCommands(DrawContext &ctx, ResourceManager *resources);
};

struct DrawContext
{
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

class SceneManager
{
private:
    std::vector<std::unique_ptr<RenderObject> > renderObjects;
    std::vector<std::shared_ptr<Node> > sceneGraphRoots;
    Camera *activeCamera = nullptr;

    GPUSceneData sceneData{};

public:
    void init();

    void cleanup();

    RenderObject *createRenderObject();

    void removeRenderObject(RenderObject *obj);

    void addSceneRoot(std::shared_ptr<Node> root) { sceneGraphRoots.push_back(root); }

    void setActiveCamera(Camera *camera) { activeCamera = camera; }
    Camera *getActiveCamera() { return activeCamera; }

    void update(float deltaTime);

    void collectDrawCommands(DrawContext &ctx, ResourceManager *resources);

    void setSunlight(const glm::vec3 &direction, const glm::vec3 &color, float intensity);

    void setAmbientLight(const glm::vec3 &color);

    const GPUSceneData &getSceneData() const { return sceneData; }

    void updateSceneData(const RenderView & view);
};

#include "vk_scene.h"

#include <utility>

#include "core/vk_engine.h"
#include "glm/gtx/transform.hpp"

void SceneManager::init(VulkanEngine *engine)
{
    _engine = engine;

    mainCamera.velocity = glm::vec3(0.f);
    mainCamera.position = glm::vec3(30.f, -00.f, -085.f);
    mainCamera.pitch = 0;
    mainCamera.yaw = 0;

    sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    sceneData.sunlightDirection = glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f);
    sceneData.sunlightColor = glm::vec4(1.0f, 1.0f, 1.0f, 3.0f);
}

void SceneManager::update_scene()
{
    auto start = std::chrono::system_clock::now();

    mainDrawContext.OpaqueSurfaces.clear();
    mainDrawContext.TransparentSurfaces.clear();

    mainCamera.update();

    if (loadedScenes.find("structure") != loadedScenes.end())
    {
        loadedScenes["structure"]->Draw(glm::mat4{1.f}, mainDrawContext);
    }

    if (_engine->cubeMesh)
    {
        const GeoSurface &surf = _engine->cubeMesh->surfaces[0];
        RenderObject obj{};
        obj.indexCount = surf.count;
        obj.firstIndex = surf.startIndex;
        obj.indexBuffer = _engine->cubeMesh->meshBuffers.indexBuffer.buffer;
        obj.vertexBufferAddress = _engine->cubeMesh->meshBuffers.vertexBufferAddress;
        obj.material = &surf.material->data;
        obj.bounds = surf.bounds;
        obj.transform = glm::translate(glm::mat4(1.f), glm::vec3(-2.f, 0.f, -2.f));
        mainDrawContext.OpaqueSurfaces.push_back(obj);
    }

    if (_engine->sphereMesh)
    {
        const auto &[startIndex, count, bounds, material] = _engine->sphereMesh->surfaces[0];
        RenderObject obj{};
        obj.indexCount = count;
        obj.firstIndex = startIndex;
        obj.indexBuffer = _engine->sphereMesh->meshBuffers.indexBuffer.buffer;
        obj.vertexBufferAddress = _engine->sphereMesh->meshBuffers.vertexBufferAddress;
        obj.material = &material->data;
        obj.bounds = bounds;
        obj.transform = glm::translate(glm::mat4(1.f), glm::vec3(2.f, 0.f, -2.f));
        mainDrawContext.OpaqueSurfaces.push_back(obj);
    }

    glm::mat4 view = mainCamera.getViewMatrix();
    glm::mat4 projection = glm::perspective(
        glm::radians(70.f),
        (float) _engine->_swapchainManager->windowExtent().width / (float) _engine->_swapchainManager->windowExtent().
        height,
        10000.f, 0.1f
    );
    projection[1][1] *= -1;

    sceneData.view = view;
    sceneData.proj = projection;
    sceneData.viewproj = projection * view;

    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    stats.scene_update_time = elapsed.count() / 1000.f;
}

void SceneManager::loadScene(const std::string &name, std::shared_ptr<LoadedGLTF> scene)
{
    loadedScenes[name] = std::move(scene);
}

std::shared_ptr<LoadedGLTF> SceneManager::getScene(const std::string &name)
{
    auto it = loadedScenes.find(name);
    return (it != loadedScenes.end()) ? it->second : nullptr;
}

void SceneManager::cleanup()
{
    loadedScenes.clear();
    loadedNodes.clear();
}

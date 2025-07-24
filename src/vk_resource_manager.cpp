#include "vk_resource_manager.h"

Handle<MeshResource> ResourceManager::createMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    auto mesh = std::make_unique<MeshResource>();

    mesh->buffers = renderer->uploadMesh(indices, vertices);

    glm::vec3 minPos = vertices[0].position;
    glm::vec3 maxPos = vertices[0].position;
    for (const auto &v: vertices)
    {
        minPos = glm::min(minPos, v.position);
        maxPos = glm::max(maxPos, v.position);
    }
    mesh->bounds.origin = (maxPos + minPos) / 2.f;
    mesh->bounds.extents = (maxPos - minPos) / 2.f;
    mesh->bounds.sphereRadius = glm::length(mesh->bounds.extents);

    return meshPool.allocate(std::move(mesh));
}
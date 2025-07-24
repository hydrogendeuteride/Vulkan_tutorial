// vk_render_view.h
#pragma once
#include "vk_types.h"
#include "camera.h"
#include "glm/ext/matrix_clip_space.hpp"

struct RenderView
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 viewProjectionMatrix;

    VkViewport viewport;
    VkRect2D scissor;

    VkExtent2D renderExtent;

    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float fov = 70.0f;

    glm::vec3 cameraPosition;
    glm::vec3 cameraDirection;

    RenderView() = default;

    static RenderView fromCamera(Camera &camera, VkExtent2D extent)
    {
        RenderView view;

        view.viewMatrix = camera.getViewMatrix();
        view.cameraPosition = camera.position;

        glm::mat4 cameraRotation = camera.getRotationMatrix();
        view.cameraDirection = glm::vec3(cameraRotation * glm::vec4(0, 0, -1, 0));

        view.projectionMatrix = glm::perspective(
            glm::radians(view.fov),
            static_cast<float>(extent.width) / (float) extent.height,
            view.farPlane,
            view.nearPlane
        );

        view.projectionMatrix[1][1] *= -1;

        view.viewProjectionMatrix = view.projectionMatrix * view.viewMatrix;

        view.viewport.x = 0.0f;
        view.viewport.y = 0.0f;
        view.viewport.width = static_cast<float>(extent.width);
        view.viewport.height = static_cast<float>(extent.height);
        view.viewport.minDepth = 0.0f;
        view.viewport.maxDepth = 1.0f;

        view.scissor.offset = {0, 0};
        view.scissor.extent = extent;

        view.renderExtent = extent;

        return view;
    }

    static RenderView fromMatrices(const glm::mat4 &view, const glm::mat4 &proj, VkExtent2D extent)
    {
        RenderView rv;
        rv.viewMatrix = view;
        rv.projectionMatrix = proj;
        rv.viewProjectionMatrix = proj * view;
        rv.renderExtent = extent;

        rv.viewport.x = 0.0f;
        rv.viewport.y = 0.0f;
        rv.viewport.width = static_cast<float>(extent.width);
        rv.viewport.height = static_cast<float>(extent.height);
        rv.viewport.minDepth = 0.0f;
        rv.viewport.maxDepth = 1.0f;

        rv.scissor.offset = {0, 0};
        rv.scissor.extent = extent;

        glm::mat4 invView = glm::inverse(view);
        rv.cameraPosition = glm::vec3(invView[3]);
        rv.cameraDirection = -glm::vec3(invView[2]);

        return rv;
    }

    void setRenderRegion(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        viewport.x = static_cast<float>(x);
        viewport.y = static_cast<float>(y);
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);

        scissor.offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)};
        scissor.extent = {width, height};
    }
};

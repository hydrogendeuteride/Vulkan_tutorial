#include "vk_renderpass_background.h"
#include "vk_engine.h"
#include "vk_images.h"
#include "vk_resource.h"

void BackgroundPass::init(VulkanEngine *engine)
{
    _engine = engine;
    init_background_pipelines();
}

void BackgroundPass::init_background_pipelines()
{
    ComputePipelineCreateInfo createInfo{};
    createInfo.shaderPath = "../shaders/gradient_color.comp.spv";
    createInfo.descriptorTypes = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    createInfo.pushConstantSize = sizeof(ComputePushConstants);
    _engine->compute.registerPipeline("gradient", createInfo);

    createInfo.shaderPath = "../shaders/sky.comp.spv";
    _engine->compute.registerPipeline("sky", createInfo);

    ComputeEffect gradient{};
    gradient.name = "gradient";
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    ComputeEffect sky{};
    sky.name = "sky";
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    _backgroundEffects.push_back(gradient);
    _backgroundEffects.push_back(sky);
}

void BackgroundPass::execute(VkCommandBuffer cmd)
{
    vkutil::transition_image(cmd, _engine->_swapchainManager->depthImage().image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _engine->_swapchainManager->depthImage().image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_IMAGE_LAYOUT_GENERAL);

    ComputeEffect &effect = _backgroundEffects[_currentEffect];

    ComputeDispatchInfo dispatchInfo = ComputeManager::createDispatch2D(
        _engine->_drawExtent.width, _engine->_drawExtent.height);
    dispatchInfo.bindings.push_back(ComputeBinding::storeImage(0, _engine->_swapchainManager->drawImage().imageView));
    dispatchInfo.pushConstants = &effect.data;
    dispatchInfo.pushConstantSize = sizeof(ComputePushConstants);

    _engine->compute.dispatch(cmd, effect.name, dispatchInfo);
}

void BackgroundPass::cleanup()
{
    if (_engine)
    {
        _engine->compute.unregisterPipeline("gradient");
        _engine->compute.unregisterPipeline("sky");
    }
    _backgroundEffects.clear();
}

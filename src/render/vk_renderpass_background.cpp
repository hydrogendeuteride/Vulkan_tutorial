#include "vk_renderpass_background.h"

#include "vk_swapchain.h"
#include "core/engine_context.h"
#include "core/vk_images.h"
#include "core/vk_resource.h"

void BackgroundPass::init(EngineContext *context)
{
    _context = context;
    init_background_pipelines();
}

void BackgroundPass::init_background_pipelines()
{
    ComputePipelineCreateInfo createInfo{};
    createInfo.shaderPath = "../shaders/gradient_color.comp.spv";
    createInfo.descriptorTypes = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    createInfo.pushConstantSize = sizeof(ComputePushConstants);
    _context->compute->registerPipeline("gradient", createInfo);

    createInfo.shaderPath = "../shaders/sky.comp.spv";
    _context->compute->registerPipeline("sky", createInfo);

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
    vkutil::transition_image(cmd, _context->getSwapchain()->depthImage().image, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _context->getSwapchain()->depthImage().image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_IMAGE_LAYOUT_GENERAL);

    ComputeEffect &effect = _backgroundEffects[_currentEffect];

    ComputeDispatchInfo dispatchInfo = ComputeManager::createDispatch2D(
        _context->drawExtent.width, _context->drawExtent.height);
    dispatchInfo.bindings.push_back(ComputeBinding::storeImage(0, _context->getSwapchain()->drawImage().imageView));
    dispatchInfo.pushConstants = &effect.data;
    dispatchInfo.pushConstantSize = sizeof(ComputePushConstants);

    _context->compute->dispatch(cmd, effect.name, dispatchInfo);
}

void BackgroundPass::cleanup()
{
    if (_context && _context->compute)
    {
        _context->compute->unregisterPipeline("gradient");
        _context->compute->unregisterPipeline("sky");
    }
    fmt::print("RenderPassManager::cleanup()\n");
    _backgroundEffects.clear();
}

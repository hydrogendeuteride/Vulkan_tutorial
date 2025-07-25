#include "vk_renderpass_manager.h"
#include "vk_renderer.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"

void RenderPassManager::executeAll(VkCommandBuffer cmd, VkExtent2D extent,
                                   SceneManager *scene, ResourceManager *resources)
{
    drawContext.OpaqueSurfaces.clear();
    drawContext.TransparentSurfaces.clear();
    scene->collectDrawCommands(drawContext, resources);

    RenderContext ctx{};
    ctx.cmd = cmd;
    ctx.extent = extent;
    ctx.scene = scene;
    ctx.resources = resources;
    ctx.sceneData = &scene->getSceneData();

    for (auto &pass: passes)
    {
        if (pass->isEnabled())
        {
            pass->execute(ctx);
        }
    }
}

void RenderPassManager::init(VulkanRenderer *renderer)
{
    auto *bg = addPass<BackgroundPass>();
    bg->initialize(renderer);

    auto *gbuffer = addPass<GBufferPass>();
    gbuffer->initialize(renderer);

    auto *light = addPass<DeferredLightingPass>();
    light->initialize(renderer);
}

void RenderPassManager::cleanup()
{
    for (auto &p : passes)
    {
        p->cleanup();
    }
    passes.clear();
}

void GBufferPass::initialize(VulkanRenderer *rendererIn)
{
    renderer = rendererIn;
}

void GBufferPass::execute(const RenderContext &ctx)
{
    (void)ctx;
}

void DeferredLightingPass::initialize(VulkanRenderer *rendererIn)
{
    renderer = rendererIn;
}

void DeferredLightingPass::execute(const RenderContext &ctx)
{
    (void)ctx;
}

void BackgroundPass::initialize(VulkanRenderer *renderer)
{
    this->renderer = renderer;
    compute = &renderer->compute;

    ComputePipelineCreateInfo createInfo{};
    createInfo.shaderPath = "../shaders/gradient_color.comp.spv";
    createInfo.descriptorTypes = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    createInfo.pushConstantSize = sizeof(ComputePushConstants);
    compute->registerPipeline("gradient", createInfo);

    createInfo.shaderPath = "../shaders/sky.comp.spv";
    compute->registerPipeline("sky", createInfo);

    ComputeEffect gradient{};
    gradient.name = "gradient";
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);
    effects.push_back(gradient);

    ComputeEffect sky{};
    sky.name = "sky";
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);
    effects.push_back(sky);
}

void BackgroundPass::execute(const RenderContext &ctx)
{
    if (!compute)
        return;

    ComputeEffect &effect = effects[currentEffect];
    ComputeDispatchInfo dispatchInfo = ComputeManager::createDispatch2D(ctx.extent.width, ctx.extent.height);
    dispatchInfo.bindings.push_back(ComputeBinding::storeImage(0, renderer->_drawImage.imageView));
    dispatchInfo.pushConstants = &effect.data;
    dispatchInfo.pushConstantSize = sizeof(ComputePushConstants);

    compute->dispatch(ctx.cmd, effect.name, dispatchInfo);
}
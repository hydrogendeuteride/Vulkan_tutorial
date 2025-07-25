// vk_renderpass_manager.h
#pragma once
#include "vk_types.h"
#include "vk_compute.h"
#include "vk_scene_manager.h"

class VulkanRenderer;
class SceneManager;
class ResourceManager;

struct ComputePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect
{
    const char *name;
    ComputePushConstants data;
};

struct RenderContext
{
    VkCommandBuffer cmd;
    VkExtent2D extent;
    SceneManager *scene;
    ResourceManager *resources;
    const GPUSceneData *sceneData;

    DescriptorAllocatorGrowable *frameDescriptors;
    std::function<void(std::function<void()>)> addFrameDeletion;
};

class RenderPass
{
public:
    virtual ~RenderPass() = default;

    virtual void initialize(VulkanRenderer *renderer)
    {
    }

    virtual void cleanup()
    {
    }

    virtual void execute(const RenderContext &ctx) = 0;

    virtual bool isEnabled() const { return enabled; }
    void setEnabled(bool value) { enabled = value; }

protected:
    bool enabled = true;
};

class GBufferPass : public RenderPass
{
private:
    ComputeManager *compute = nullptr;
    VulkanRenderer *renderer = nullptr;

    VkPipeline pipeline = nullptr;
    VkPipelineLayout pipelineLayout = nullptr;
    VkDescriptorSetLayout descriptorLayout = nullptr;

public:
    void initialize(VulkanRenderer *renderer) override;

    void execute(const RenderContext &ctx) override;
};

class DeferredLightingPass : public RenderPass
{
private:
    VulkanRenderer *renderer = nullptr;
    VkPipeline lightingPipeline = nullptr;
    VkPipelineLayout lightingPipelineLayout = nullptr;
    VkDescriptorSet gBufferInputDescriptorSet = nullptr;

public:
    void initialize(VulkanRenderer *renderer) override;

    void execute(const RenderContext &ctx) override;
};

class BackgroundPass : public RenderPass
{
private:
    ComputeManager *compute = nullptr;
    VulkanRenderer *renderer = nullptr;
    std::vector<ComputeEffect> effects;
    int currentEffect = 0;

public:
    void initialize(VulkanRenderer *renderer) override;

    void execute(const RenderContext &ctx) override;

    void setEffect(int index) { currentEffect = index; }
};

class RenderPassManager
{
private:
    std::vector<std::unique_ptr<RenderPass> > passes;
    DrawContext drawContext;

public:
    void init(VulkanRenderer *renderer);

    void cleanup();

    template<typename T>
    T *addPass()
    {
        auto pass = std::make_unique<T>();
        T *ptr = pass.get();
        passes.push_back(std::move(pass));
        return ptr;
    }

    void executeAll(VkCommandBuffer cmd, VkExtent2D extent, SceneManager *scene, ResourceManager *resources);
};

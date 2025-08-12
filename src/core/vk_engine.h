// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <core/vk_types.h>
#include <vector>
#include <string>
#include "vk_mem_alloc.h"
#include <deque>
#include <functional>
#include "vk_descriptors.h"
#include "scene/vk_loader.h"
#include "compute/vk_compute.h"
#include <scene/camera.h>

#include "vk_device.h"
#include "render/vk_renderpass.h"
#include "render/vk_renderpass_background.h"
#include "vk_resource.h"
#include "vk_swapchain.h"
#include "scene/vk_scene.h"
#include "render/vk_materials.h"

#include "frame_resources.h"

constexpr unsigned int FRAME_OVERLAP = 2;

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


struct RenderPass
{
	std::string name;
	std::function<void(VkCommandBuffer)> execute;
};

struct EngineStats
{
	float frametime;
	int triangle_count;
	int drawcall_count;
	float scene_update_time;
	float mesh_draw_time;
};

struct MeshNode : public Node
{
	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4 &topMatrix, DrawContext &ctx) override;
};

class VulkanEngine
{
public:
	bool _isInitialized{false};
	int _frameNumber{0};

	std::unique_ptr<DeviceManager> _deviceManager;
	std::unique_ptr<SwapchainManager> _swapchainManager;
	std::unique_ptr<ResourceManager> _resourceManager;
	std::unique_ptr<RenderPassManager> _renderPassManager;
	std::unique_ptr<SceneManager> _sceneManager;

	struct SDL_Window *_window{nullptr};

    FrameResources _frames[FRAME_OVERLAP];

    FrameResources &get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

	VkExtent2D _drawExtent;
	float renderScale = 1.f;

	DescriptorAllocatorGrowable globalDescriptorAllocator;
	ComputeManager compute;

	std::vector<VkFramebuffer> _framebuffers;

	VkDescriptorSetLayout _singleImageDescriptorLayout;

	DeletionQueue _mainDeletionQueue;

	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	GPUMeshBuffers rectangle;

	std::shared_ptr<MeshAsset> cubeMesh;
	std::shared_ptr<MeshAsset> sphereMesh;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	MaterialInstance defaultData;

    GLTFMetallic_Roughness metalRoughMaterial;

	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

	EngineStats stats;

	std::vector<RenderPass> renderPasses;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	bool resize_requested{false};
	bool freeze_rendering{false};

private:
    void init_frame_resources();
    void init_commands();
    void init_pipelines();

	void init_mesh_pipeline();

    void init_deferred_pipelines(); // TODO: move remaining pipeline setup into passes

    void init_descriptors();

	void init_default_samplers();

	void init_default_data();
};

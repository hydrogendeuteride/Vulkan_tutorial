// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <string>
#include "vk_mem_alloc.h"
#include <deque>
#include <functional>
#include "vk_descriptors.h"
#include "vk_loader.h"
#include "vk_compute.h"
#include <camera.h>

#include "vk_device.h"
#include "vk_renderpass.h"
#include "vk_swapchain.h"

struct FrameData
{
	VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	DeletionQueue _deletionQueue;
	DescriptorAllocatorGrowable _frameDescriptors;
};

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

struct GLTFMetallic_Roughness
{
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;
	MaterialPipeline gBufferPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants
	{
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;

		glm::vec4 extra[14];
	};

	struct MaterialResources
	{
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void build_pipelines(VulkanEngine *engine);

	void clear_resources(VkDevice device) const;

	MaterialInstance write_material(VkDevice device, MaterialPass pass, const MaterialResources &resources,
	                                DescriptorAllocatorGrowable &descriptorAllocator);
};

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

	Camera mainCamera;

	VkExtent2D _windowExtent{1920, 1080};

	struct SDL_Window *_window{nullptr};

	FrameData _frames[FRAME_OVERLAP];

	FrameData &get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

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
	DrawContext drawCommands;

	std::shared_ptr<MeshAsset> cubeMesh;
	std::shared_ptr<MeshAsset> sphereMesh;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	//draw resources
	DrawContext mainDrawContext;
	MaterialInstance defaultData;

	GLTFMetallic_Roughness metalRoughMaterial;

	VkDescriptorSetLayout _gBufferInputDescriptorLayout;
	VkDescriptorSet _gBufferInputDescriptorSet;

	VkPipelineLayout _gBufferPipelineLayout;
	VkPipeline _gBufferPipeline;

	VkPipelineLayout _lightingPipelineLayout;
	VkPipeline _lightingPipeline;

	std::unordered_map<std::string, std::shared_ptr<Node> > loadedNodes;

	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF> > loadedScenes;

	GPUSceneData sceneData;
	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

	EngineStats stats;

	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{0};

	std::vector<RenderPass> renderPasses;

	void add_render_pass(const std::string &name, std::function<void(VkCommandBuffer)> func);

	void init_render_passes();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	void draw_background(VkCommandBuffer cmd);

	void draw_geometry(VkCommandBuffer cmd);

	void draw_lighting(VkCommandBuffer cmd);

	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView) const;

	void update_scene();

	//run main loop
	void run();

	bool resize_requested{false};
	bool freeze_rendering{false};

private:
	void init_commands();

	void init_background_pipelines();

	void init_pipelines();

	void init_mesh_pipeline();

	void init_deferred_pipelines();

	void init_descriptors();

	void init_sync_structures();

	void init_imgui();

	void init_default_samplers();

	void init_default_data();
};

#pragma once

#include <core/vk_types.h>
#include <render/rg_types.h>
#include <render/rg_resources.h>
#include <render/rg_builder.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class EngineContext;

class RenderGraph
{
public:
	void init(EngineContext* ctx);
	void clear();

	// Import externally owned images (swapchain, drawImage, g-buffers)
	RGImageHandle import_image(const RGImportedImageDesc& desc);

	// Create transient images (not used in v1 skeleton; stubbed for future)
	RGImageHandle create_image(const RGImageDesc& desc);

	// Buffer import/create helpers
	RGBufferHandle import_buffer(const RGImportedBufferDesc& desc);
	RGBufferHandle create_buffer(const RGBufferDesc& desc);

	// Pass builder API
struct Pass; // fwd
	using RecordCallback = std::function<void(VkCommandBuffer cmd, const class RGPassResources& res, EngineContext* ctx)>;
	using BuildCallback  = std::function<void(class RGPassBuilder& b, EngineContext* ctx)>;

	void add_pass(const char* name, RGPassType type, BuildCallback build, RecordCallback record);
	// Legacy simple add
	void add_pass(const char* name, RGPassType type, RecordCallback record);

	// Build internal state for this frame (no-op in v1)
	bool compile();

	// Execute in insertion order (no barriers yet)
	void execute(VkCommandBuffer cmd);

	// Convenience import helpers (read from EngineContext::swapchain)
	RGImageHandle import_draw_image();
	RGImageHandle import_depth_image();
	RGImageHandle import_gbuffer_position();
	RGImageHandle import_gbuffer_normal();
	RGImageHandle import_gbuffer_albedo();
	RGImageHandle import_swapchain_image(uint32_t index);
	void add_present_chain(RGImageHandle sourceDraw,
	                       RGImageHandle targetSwapchain,
	                       std::function<void(RenderGraph&)> appendExtra = {});

private:
	struct ImportedImage
	{
		RGImportedImageDesc desc;
		RGImageHandle handle;
	};

	struct Pass
	{
		std::string name;
		RGPassType type{};
		RecordCallback record;

		// Declarations
		std::vector<RGPassImageAccess> imageReads;
		std::vector<RGPassImageAccess> imageWrites;
		std::vector<RGPassBufferAccess> bufferReads;
		std::vector<RGPassBufferAccess> bufferWrites;
		std::vector<RGAttachmentInfo> colorAttachments;
		bool hasDepth = false;
		RGAttachmentInfo depthAttachment{};

		std::vector<VkImageMemoryBarrier2> preImageBarriers;
		std::vector<VkBufferMemoryBarrier2> preBufferBarriers;
	};

	EngineContext* _context = nullptr;
	RGResourceRegistry _resources;
	std::vector<Pass> _passes;
};

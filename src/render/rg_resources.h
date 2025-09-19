#pragma once

#include <core/vk_types.h>
#include <render/rg_types.h>
#include <string>
#include <vector>

class EngineContext;

struct RGImageRecord
{
	std::string name;
	bool imported = true;

	// Unified view for either imported or transient
	VkImage image = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent2D extent{0, 0};
	VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// If transient, keep allocation owner for cleanup
	AllocatedImage allocation{};
};

struct RGBufferRecord
{
	std::string name;
	bool imported = true;

	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
	VkBufferUsageFlags usage = 0;
	VkPipelineStageFlags2 initialStage = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2 initialAccess = 0;

	AllocatedBuffer allocation{};
};

class RGResourceRegistry
{
public:
	void init(EngineContext* ctx) { _ctx = ctx; }

	void reset();

	RGImageHandle add_imported(const RGImportedImageDesc& d);
	RGImageHandle add_transient(const RGImageDesc& d);

	RGBufferHandle add_imported(const RGImportedBufferDesc& d);
	RGBufferHandle add_transient(const RGBufferDesc& d);

	const RGImageRecord* get_image(RGImageHandle h) const;
	RGImageRecord* get_image(RGImageHandle h);

	const RGBufferRecord* get_buffer(RGBufferHandle h) const;
	RGBufferRecord* get_buffer(RGBufferHandle h);

	size_t image_count() const { return _images.size(); }
	size_t buffer_count() const { return _buffers.size(); }

	VkImageLayout initial_layout(RGImageHandle h) const;
	VkFormat image_format(RGImageHandle h) const;

	VkPipelineStageFlags2 initial_stage(RGBufferHandle h) const;
	VkAccessFlags2 initial_access(RGBufferHandle h) const;

private:
	EngineContext* _ctx = nullptr;
	std::vector<RGImageRecord> _images;
	std::vector<RGBufferRecord> _buffers;
};

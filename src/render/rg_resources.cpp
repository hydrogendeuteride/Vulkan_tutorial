#include <render/rg_resources.h>
#include <core/engine_context.h>
#include <core/vk_resource.h>

#include "frame_resources.h"

void RGResourceRegistry::reset()
{
	_images.clear();
	_buffers.clear();
}

RGImageHandle RGResourceRegistry::add_imported(const RGImportedImageDesc& d)
{
	RGImageRecord rec{};
	rec.name = d.name;
	rec.imported = true;
	rec.image = d.image;
	rec.imageView = d.imageView;
	rec.format = d.format;
	rec.extent = d.extent;
	rec.initialLayout = d.currentLayout;
	_images.push_back(rec);
	return RGImageHandle{ static_cast<uint32_t>(_images.size() - 1) };
}

RGImageHandle RGResourceRegistry::add_transient(const RGImageDesc& d)
{
	RGImageRecord rec{};
	rec.name = d.name;
	rec.imported = false;
	rec.format = d.format;
	rec.extent = d.extent;
	rec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkExtent3D size{ d.extent.width, d.extent.height, 1 };
	rec.allocation = _ctx->getResources()->create_image(size, d.format, d.usage);
	rec.image = rec.allocation.image;
	rec.imageView = rec.allocation.imageView;

	// Cleanup at end of frame
	if (_ctx && _ctx->currentFrame)
	{
		auto img = rec.allocation;
		_ctx->currentFrame->_deletionQueue.push_function([ctx=_ctx, img]() {
			ctx->getResources()->destroy_image(img);
		});
	}

	_images.push_back(rec);
	return RGImageHandle{ static_cast<uint32_t>(_images.size() - 1) };
}

RGBufferHandle RGResourceRegistry::add_imported(const RGImportedBufferDesc& d)
{
	RGBufferRecord rec{};
	rec.name = d.name;
	rec.imported = true;
	rec.buffer = d.buffer;
	rec.size = d.size;
	rec.initialStage = d.currentStage;
	rec.initialAccess = d.currentAccess;
	_buffers.push_back(rec);
	return RGBufferHandle{ static_cast<uint32_t>(_buffers.size() - 1) };
}

RGBufferHandle RGResourceRegistry::add_transient(const RGBufferDesc& d)
{
	RGBufferRecord rec{};
	rec.name = d.name;
	rec.imported = false;
	rec.size = d.size;
	rec.usage = d.usage;
	rec.initialStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
	rec.initialAccess = 0;

	rec.allocation = _ctx->getResources()->create_buffer(d.size, d.usage, d.memoryUsage);
	rec.buffer = rec.allocation.buffer;

	if (_ctx && _ctx->currentFrame)
	{
		auto buf = rec.allocation;
		_ctx->currentFrame->_deletionQueue.push_function([ctx=_ctx, buf]() {
			ctx->getResources()->destroy_buffer(buf);
		});
	}

	_buffers.push_back(rec);
	return RGBufferHandle{ static_cast<uint32_t>(_buffers.size() - 1) };
}

const RGImageRecord* RGResourceRegistry::get_image(RGImageHandle h) const
{
	if (!h.valid() || h.id >= _images.size()) return nullptr;
	return &_images[h.id];
}

RGImageRecord* RGResourceRegistry::get_image(RGImageHandle h)
{
	if (!h.valid() || h.id >= _images.size()) return nullptr;
	return &_images[h.id];
}

const RGBufferRecord* RGResourceRegistry::get_buffer(RGBufferHandle h) const
{
	if (!h.valid() || h.id >= _buffers.size()) return nullptr;
	return &_buffers[h.id];
}

RGBufferRecord* RGResourceRegistry::get_buffer(RGBufferHandle h)
{
	if (!h.valid() || h.id >= _buffers.size()) return nullptr;
	return &_buffers[h.id];
}

VkImageLayout RGResourceRegistry::initial_layout(RGImageHandle h) const
{
	const RGImageRecord* rec = get_image(h);
	return rec ? rec->initialLayout : VK_IMAGE_LAYOUT_UNDEFINED;
}

VkFormat RGResourceRegistry::image_format(RGImageHandle h) const
{
	const RGImageRecord* rec = get_image(h);
	return rec ? rec->format : VK_FORMAT_UNDEFINED;
}

VkPipelineStageFlags2 RGResourceRegistry::initial_stage(RGBufferHandle h) const
{
	const RGBufferRecord* rec = get_buffer(h);
	return rec ? rec->initialStage : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
}

VkAccessFlags2 RGResourceRegistry::initial_access(RGBufferHandle h) const
{
	const RGBufferRecord* rec = get_buffer(h);
	return rec ? rec->initialAccess : VkAccessFlags2{0};
}

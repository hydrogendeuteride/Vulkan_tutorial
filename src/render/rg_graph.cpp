#include <render/rg_graph.h>
#include <core/engine_context.h>
#include <core/vk_images.h>
#include <core/vk_initializers.h>

#include <unordered_map>

#include <core/vk_swapchain.h>

void RenderGraph::init(EngineContext *ctx)
{
	_context = ctx;
	_resources.init(ctx);
}

void RenderGraph::clear()
{
	_passes.clear();
	_resources.reset();
}

RGImageHandle RenderGraph::import_image(const RGImportedImageDesc &desc)
{
	return _resources.add_imported(desc);
}

RGBufferHandle RenderGraph::import_buffer(const RGImportedBufferDesc &desc)
{
	return _resources.add_imported(desc);
}

RGImageHandle RenderGraph::create_image(const RGImageDesc &desc)
{
	return _resources.add_transient(desc);
}

RGBufferHandle RenderGraph::create_buffer(const RGBufferDesc &desc)
{
	return _resources.add_transient(desc);
}

void RenderGraph::add_pass(const char *name, RGPassType type, BuildCallback build, RecordCallback record)
{
	Pass p{};
	p.name = name;
	p.type = type;
	p.record = std::move(record);

	// Build declarations via builder
	RGAttachmentInfo *depthRef = nullptr;
	RGPassBuilder builder(&_resources,
	                      p.imageReads,
	                      p.imageWrites,
	                      p.bufferReads,
	                      p.bufferWrites,
	                      p.colorAttachments,
	                      depthRef);
	if (build) build(builder, _context);
	if (depthRef)
	{
		p.hasDepth = true;
		p.depthAttachment = *depthRef; // copy declared depth attachment
	}

	_passes.push_back(std::move(p));
}

void RenderGraph::add_pass(const char *name, RGPassType type, RecordCallback record)
{
	// No declarations
	add_pass(name, type, nullptr, std::move(record));
}

bool RenderGraph::compile()
{
	if (!_context) return false;

	struct ImageState
	{
		bool initialized = false;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 access = 0;
	};

	struct BufferState
	{
		bool initialized = false;
		VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 access = 0;
	};

	auto is_depth_format = [](VkFormat format) {
		switch (format)
		{
			case VK_FORMAT_D16_UNORM:
			case VK_FORMAT_D16_UNORM_S8_UINT:
			case VK_FORMAT_D24_UNORM_S8_UINT:
			case VK_FORMAT_D32_SFLOAT:
			case VK_FORMAT_D32_SFLOAT_S8_UINT:
				return true;
			default:
				return false;
		}
	};

	struct ImageUsageInfo
	{
		VkPipelineStageFlags2 stage;
		VkAccessFlags2 access;
		VkImageLayout layout;
	};

	struct BufferUsageInfo
	{
		VkPipelineStageFlags2 stage;
		VkAccessFlags2 access;
	};

	auto usage_info_image = [](RGImageUsage usage) {
		ImageUsageInfo info{};
		switch (usage)
		{
			case RGImageUsage::SampledFragment:
				info.stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				info.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
				info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				break;
			case RGImageUsage::SampledCompute:
				info.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				info.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
				info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				break;
			case RGImageUsage::TransferSrc:
				info.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				info.access = VK_ACCESS_2_TRANSFER_READ_BIT;
				info.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				break;
			case RGImageUsage::TransferDst:
				info.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				info.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				info.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				break;
			case RGImageUsage::ColorAttachment:
				info.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
				info.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
				info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				break;
			case RGImageUsage::DepthAttachment:
				info.stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
				info.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
				              VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
				info.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				break;
			case RGImageUsage::ComputeWrite:
				info.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
				info.layout = VK_IMAGE_LAYOUT_GENERAL;
				break;
			case RGImageUsage::Present:
				info.stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
				info.access = VK_ACCESS_2_MEMORY_READ_BIT;
				info.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
				break;
			default:
				info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				info.access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
				info.layout = VK_IMAGE_LAYOUT_GENERAL;
				break;
		}
		return info;
	};

	auto usage_info_buffer = [](RGBufferUsage usage) {
		BufferUsageInfo info{};
		switch (usage)
		{
			case RGBufferUsage::TransferSrc:
				info.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				info.access = VK_ACCESS_2_TRANSFER_READ_BIT;
				break;
			case RGBufferUsage::TransferDst:
				info.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				info.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				break;
			case RGBufferUsage::VertexRead:
				info.stage = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
				info.access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
				break;
			case RGBufferUsage::IndexRead:
				info.stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
				info.access = VK_ACCESS_2_INDEX_READ_BIT;
				break;
			case RGBufferUsage::UniformRead:
				info.stage = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
				info.access = VK_ACCESS_2_UNIFORM_READ_BIT;
				break;
			case RGBufferUsage::StorageRead:
				info.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
				break;
			case RGBufferUsage::StorageReadWrite:
				info.stage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
				info.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
				break;
			case RGBufferUsage::IndirectArgs:
				info.stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
				info.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
				break;
			default:
				info.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				info.access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
				break;
		}
		return info;
	};

	const size_t imageCount = _resources.image_count();
	const size_t bufferCount = _resources.buffer_count();
	std::vector<ImageState> imageStates(imageCount);
	std::vector<BufferState> bufferStates(bufferCount);

	for (auto &pass: _passes)
	{
		pass.preImageBarriers.clear();
		pass.preBufferBarriers.clear();

		std::unordered_map<uint32_t, RGImageUsage> desiredImageUsages;
		desiredImageUsages.reserve(pass.imageReads.size() + pass.imageWrites.size());

		for (const auto &access: pass.imageReads)
		{
			if (!access.image.valid()) continue;
			desiredImageUsages.emplace(access.image.id, access.usage);
		}
		for (const auto &access: pass.imageWrites)
		{
			if (!access.image.valid()) continue;
			desiredImageUsages[access.image.id] = access.usage;
		}

		for (const auto &[id, usage]: desiredImageUsages)
		{
			if (id >= imageCount) continue;

			ImageUsageInfo desired = usage_info_image(usage);

			ImageState prev = imageStates[id];
			VkImageLayout prevLayout = prev.initialized ? prev.layout : _resources.initial_layout(RGImageHandle{id});
			VkPipelineStageFlags2 srcStage = prev.initialized
				                                 ? prev.stage
				                                 : (prevLayout == VK_IMAGE_LAYOUT_UNDEFINED
					                                    ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
					                                    : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
			VkAccessFlags2 srcAccess = prev.initialized
				                           ? prev.access
				                           : (prevLayout == VK_IMAGE_LAYOUT_UNDEFINED
					                              ? VkAccessFlags2{0}
					                              : (VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT));

			bool needBarrier = !prev.initialized
			                   || prevLayout != desired.layout
			                   || prev.stage != desired.stage
			                   || prev.access != desired.access;

			if (needBarrier)
			{
				VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
				barrier.srcStageMask = srcStage;
				barrier.srcAccessMask = srcAccess;
				barrier.dstStageMask = desired.stage;
				barrier.dstAccessMask = desired.access;
				barrier.oldLayout = prevLayout;
				barrier.newLayout = desired.layout;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

				const RGImageRecord *rec = _resources.get_image(RGImageHandle{id});
				barrier.image = rec ? rec->image : VK_NULL_HANDLE;

				VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
				if (usage == RGImageUsage::DepthAttachment || (rec && is_depth_format(rec->format)))
				{
					aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
				}
				barrier.subresourceRange = vkinit::image_subresource_range(aspect);
				pass.preImageBarriers.push_back(barrier);
			}

			imageStates[id].initialized = true;
			imageStates[id].layout = desired.layout;
			imageStates[id].stage = desired.stage;
			imageStates[id].access = desired.access;
		}

		if (bufferCount == 0) continue;

		std::unordered_map<uint32_t, RGBufferUsage> desiredBufferUsages;
		desiredBufferUsages.reserve(pass.bufferReads.size() + pass.bufferWrites.size());

		for (const auto &access: pass.bufferReads)
		{
			if (!access.buffer.valid()) continue;
			desiredBufferUsages.emplace(access.buffer.id, access.usage);
		}
		for (const auto &access: pass.bufferWrites)
		{
			if (!access.buffer.valid()) continue;
			desiredBufferUsages[access.buffer.id] = access.usage;
		}

		for (const auto &[id, usage]: desiredBufferUsages)
		{
			if (id >= bufferCount) continue;

			BufferUsageInfo desired = usage_info_buffer(usage);

			BufferState prev = bufferStates[id];
			VkPipelineStageFlags2 srcStage = prev.initialized
				                                 ? prev.stage
				                                 : _resources.initial_stage(RGBufferHandle{id});
			if (srcStage == VK_PIPELINE_STAGE_2_NONE)
			{
				srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			}
			VkAccessFlags2 srcAccess = prev.initialized
				                           ? prev.access
				                           : _resources.initial_access(RGBufferHandle{id});

			bool needBarrier = !prev.initialized
			                   || prev.stage != desired.stage
			                   || prev.access != desired.access;

			if (needBarrier)
			{
				VkBufferMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
				barrier.srcStageMask = srcStage;
				barrier.srcAccessMask = srcAccess;
				barrier.dstStageMask = desired.stage;
				barrier.dstAccessMask = desired.access;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

				const RGBufferRecord *rec = _resources.get_buffer(RGBufferHandle{id});
				barrier.buffer = rec ? rec->buffer : VK_NULL_HANDLE;
				barrier.offset = 0;
				barrier.size = rec ? rec->size : VK_WHOLE_SIZE;
				pass.preBufferBarriers.push_back(barrier);
			}

			bufferStates[id].initialized = true;
			bufferStates[id].stage = desired.stage;
			bufferStates[id].access = desired.access;
		}
	}

	return true;
}

void RenderGraph::execute(VkCommandBuffer cmd)
{
	for (auto &p: _passes)
	{
		if (!p.preImageBarriers.empty() || !p.preBufferBarriers.empty())
		{
			VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
			dep.imageMemoryBarrierCount = static_cast<uint32_t>(p.preImageBarriers.size());
			dep.pImageMemoryBarriers = p.preImageBarriers.empty() ? nullptr : p.preImageBarriers.data();
			dep.bufferMemoryBarrierCount = static_cast<uint32_t>(p.preBufferBarriers.size());
			dep.pBufferMemoryBarriers = p.preBufferBarriers.empty() ? nullptr : p.preBufferBarriers.data();
			vkCmdPipelineBarrier2(cmd, &dep);
		}

		if (p.record)
		{
			RGPassResources res(&_resources);
			p.record(cmd, res, _context);
		}
	}
}

// --- Import helpers ---
void RenderGraph::add_present_chain(RGImageHandle sourceDraw,
                                    RGImageHandle targetSwapchain,
                                    std::function<void(RenderGraph &)> appendExtra)
{
	if (!sourceDraw.valid() || !targetSwapchain.valid()) return;

	add_pass(
		"CopyToSwapchain",
		RGPassType::Transfer,
		[sourceDraw, targetSwapchain](RGPassBuilder &builder, EngineContext *) {
			builder.read(sourceDraw, RGImageUsage::TransferSrc);
			builder.write(targetSwapchain, RGImageUsage::TransferDst);
		},
		[sourceDraw, targetSwapchain](VkCommandBuffer cmd, const RGPassResources &res, EngineContext *ctx) {
			VkImage src = res.image(sourceDraw);
			VkImage dst = res.image(targetSwapchain);
			if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
			vkutil::copy_image_to_image(cmd, src, dst, ctx->getDrawExtent(), ctx->getSwapchain()->swapchainExtent());
		});

	if (appendExtra)
	{
		appendExtra(*this);
	}

	add_pass(
		"PreparePresent",
		RGPassType::Transfer,
		[targetSwapchain](RGPassBuilder &builder, EngineContext *) {
			builder.write(targetSwapchain, RGImageUsage::Present);
		},
		[](VkCommandBuffer, const RGPassResources &, EngineContext *) {
		});
}

RGImageHandle RenderGraph::import_draw_image()
{
	RGImportedImageDesc d{};
	d.name = "drawImage";
	d.image = _context->getSwapchain()->drawImage().image;
	d.imageView = _context->getSwapchain()->drawImage().imageView;
	d.format = _context->getSwapchain()->drawImage().imageFormat;
	d.extent = _context->getDrawExtent();
	d.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
	return import_image(d);
}

RGImageHandle RenderGraph::import_depth_image()
{
	RGImportedImageDesc d{};
	d.name = "depthImage";
	d.image = _context->getSwapchain()->depthImage().image;
	d.imageView = _context->getSwapchain()->depthImage().imageView;
	d.format = _context->getSwapchain()->depthImage().imageFormat;
	d.extent = _context->getDrawExtent();
	d.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return import_image(d);
}

RGImageHandle RenderGraph::import_gbuffer_position()
{
	RGImportedImageDesc d{};
	d.name = "gBuffer.position";
	d.image = _context->getSwapchain()->gBufferPosition().image;
	d.imageView = _context->getSwapchain()->gBufferPosition().imageView;
	d.format = _context->getSwapchain()->gBufferPosition().imageFormat;
	d.extent = _context->getDrawExtent();
	d.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return import_image(d);
}

RGImageHandle RenderGraph::import_gbuffer_normal()
{
	RGImportedImageDesc d{};
	d.name = "gBuffer.normal";
	d.image = _context->getSwapchain()->gBufferNormal().image;
	d.imageView = _context->getSwapchain()->gBufferNormal().imageView;
	d.format = _context->getSwapchain()->gBufferNormal().imageFormat;
	d.extent = _context->getDrawExtent();
	d.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return import_image(d);
}

RGImageHandle RenderGraph::import_gbuffer_albedo()
{
	RGImportedImageDesc d{};
	d.name = "gBuffer.albedo";
	d.image = _context->getSwapchain()->gBufferAlbedo().image;
	d.imageView = _context->getSwapchain()->gBufferAlbedo().imageView;
	d.format = _context->getSwapchain()->gBufferAlbedo().imageFormat;
	d.extent = _context->getDrawExtent();
	d.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return import_image(d);
}

RGImageHandle RenderGraph::import_swapchain_image(uint32_t index)
{
	RGImportedImageDesc d{};
	d.name = "swapchain.image";
	const auto &views = _context->getSwapchain()->swapchainImageViews();
	const auto &imgs = _context->getSwapchain()->swapchainImages();
	d.image = imgs[index];
	d.imageView = views[index];
	d.format = _context->getSwapchain()->swapchainImageFormat();
	d.extent = _context->getSwapchain()->swapchainExtent();
	d.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	return import_image(d);
}

#include <render/rg_resources.h>
#include <core/engine_context.h>
#include <core/vk_resource.h>

#include "frame_resources.h"

void RGResourceRegistry::reset()
{
    _images.clear();
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

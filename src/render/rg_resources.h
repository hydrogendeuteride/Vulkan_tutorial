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

class RGResourceRegistry
{
public:
    void init(EngineContext* ctx) { _ctx = ctx; }

    void reset();

    RGImageHandle add_imported(const RGImportedImageDesc& d);
    RGImageHandle add_transient(const RGImageDesc& d);

    const RGImageRecord* get_image(RGImageHandle h) const;
    RGImageRecord* get_image(RGImageHandle h);

    size_t image_count() const { return _images.size(); }

    VkImageLayout initial_layout(RGImageHandle h) const;
    VkFormat image_format(RGImageHandle h) const;

private:
    EngineContext* _ctx = nullptr;
    std::vector<RGImageRecord> _images;
};

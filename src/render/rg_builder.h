#pragma once

#include <render/rg_types.h>
#include <vector>

class RGResourceRegistry;
class EngineContext;

struct RGPassResourceAccess
{
    RGImageHandle image;
    RGImageUsage usage;
};

// Read-only interface for pass record callbacks to fetch resolved resources
class RGPassResources
{
public:
    RGPassResources(const RGResourceRegistry *registry) : _registry(registry)
    {
    }

    VkImage image(RGImageHandle h) const;

    VkImageView image_view(RGImageHandle h) const;

private:
    const RGResourceRegistry *_registry;
};

// Builder used inside add_*_pass setup lambda to declare reads/writes/attachments
class RGPassBuilder
{
public:
    RGPassBuilder(RGResourceRegistry *registry,
                  std::vector<RGPassResourceAccess> &reads,
                  std::vector<RGPassResourceAccess> &writes,
                  std::vector<RGAttachmentInfo> &colorAttachments,
                  RGAttachmentInfo *&depthAttachmentRef)
        : _registry(registry)
          , _reads(reads)
          , _writes(writes)
          , _colors(colorAttachments)
          , _depthRef(depthAttachmentRef)
    {
    }

    // Declare that the pass will sample/read an image
    void read(RGImageHandle h, RGImageUsage usage);

    // Declare that the pass will write to an image
    void write(RGImageHandle h, RGImageUsage usage);

    // Graphics attachments
    void write_color(RGImageHandle h, bool clearOnLoad = false, VkClearValue clear = {});

    void write_depth(RGImageHandle h, bool clearOnLoad = false, VkClearValue clear = {});

private:
    RGResourceRegistry *_registry;
    std::vector<RGPassResourceAccess> &_reads;
    std::vector<RGPassResourceAccess> &_writes;
    std::vector<RGAttachmentInfo> &_colors;
    RGAttachmentInfo *&_depthRef;
    RGAttachmentInfo _depthTemp{}; // temporary storage used during build
};

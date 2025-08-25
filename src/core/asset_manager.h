#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <filesystem>
#include <vector>

#include <scene/vk_loader.h>
#include <core/vk_types.h>

#include "vk_materials.h"

class VulkanEngine;
class MeshAsset;

struct AssetPaths
{
    std::filesystem::path root;
    std::filesystem::path assets;
    std::filesystem::path shaders;

    bool valid() const
    {
        return (!assets.empty() && std::filesystem::exists(assets)) || (
                   !shaders.empty() && std::filesystem::exists(shaders));
    }

    static AssetPaths detect(const std::filesystem::path &startDir = std::filesystem::current_path());
};

class AssetManager
{
public:
    struct MaterialOptions
    {
        std::string albedoPath;
        std::string metalRoughPath;

        bool albedoSRGB = true;
        bool metalRoughSRGB = false;

        GLTFMetallic_Roughness::MaterialConstants constants{};

        MaterialPass pass = MaterialPass::MainColor;
    };

    void init(VulkanEngine *engine);

    void cleanup();

    std::string shaderPath(std::string_view name) const;

    std::string modelPath(std::string_view name) const;
    std::string assetPath(std::string_view name) const;

    std::optional<std::shared_ptr<LoadedGLTF> > loadGLTF(std::string_view nameOrPath);

    std::shared_ptr<MeshAsset> getPrimitive(std::string_view name) const;

    std::shared_ptr<MeshAsset> createMesh(const std::string &name,
                                          std::span<Vertex> vertices,
                                          std::span<uint32_t> indices,
                                          std::shared_ptr<GLTFMaterial> material = {});

    std::shared_ptr<MeshAsset> createCube(const std::string &name);

    std::shared_ptr<MeshAsset> createSphere(const std::string &name, int sectors = 16, int stacks = 16);

    std::shared_ptr<MeshAsset> createTexturedMesh(const std::string &name,
                                                  std::span<Vertex> vertices,
                                                  std::span<uint32_t> indices,
                                                  std::string_view albedoImage,
                                                  bool srgb = false);

    std::shared_ptr<MeshAsset> createTexturedMesh(const std::string &name,
                                                  std::span<Vertex> vertices,
                                                  std::span<uint32_t> indices,
                                                  const MaterialOptions &options);

    std::shared_ptr<MeshAsset> createTexturedCube(const std::string &name,
                                                  std::string_view albedoImage,
                                                  bool srgb = false);

    std::shared_ptr<MeshAsset> createTexturedCube(const std::string &name,
                                                  const MaterialOptions &options);

    std::shared_ptr<MeshAsset> createTexturedSphere(const std::string &name,
                                                    std::string_view albedoImage,
                                                    int sectors = 16, int stacks = 16,
                                                    bool srgb = false);

    std::shared_ptr<MeshAsset> createTexturedSphere(const std::string &name,
                                                    const MaterialOptions &options,
                                                    int sectors = 16, int stacks = 16);

    std::shared_ptr<MeshAsset> getMesh(const std::string &name) const;

    bool removeMesh(const std::string &name);

    const AssetPaths &paths() const { return _paths; }
    void setPaths(const AssetPaths &p) { _paths = p; }

private:
    VulkanEngine *_engine = nullptr; // non-owning
    AssetPaths _paths{};

    std::unordered_map<std::string, std::weak_ptr<LoadedGLTF> > _gltfCacheByPath;
    std::unordered_map<std::string, std::shared_ptr<MeshAsset> > _meshCache;
    std::unordered_map<std::string, AllocatedBuffer> _meshMaterialBuffers;
    std::unordered_map<std::string, std::vector<AllocatedImage> > _meshOwnedImages;

    static bool file_exists(const std::filesystem::path &p);

    static std::string resolve_in(const std::filesystem::path &base, std::string_view name);
};

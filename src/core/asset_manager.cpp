#include "asset_manager.h"

#include <cstdlib>
#include <iostream>

#include <core/vk_engine.h>
#include <core/vk_resource.h>
#include <render/vk_materials.h>
#include <render/primitives.h>
#include <stb_image.h>

using std::filesystem::path;

static path get_env_path(const char *name)
{
    const char *v = std::getenv(name);
    if (!v || !*v) return {};
    path p = v;
    if (std::filesystem::exists(p)) return std::filesystem::canonical(p);
    return {};
}

static path find_upwards_containing(path start, const std::string &subdir, int maxDepth = 6)
{
    path cur = std::filesystem::weakly_canonical(start);
    for (int i = 0; i <= maxDepth; i++)
    {
        path candidate = cur / subdir;
        if (std::filesystem::exists(candidate)) return cur;
        if (!cur.has_parent_path()) break;
        cur = cur.parent_path();
    }
    return {};
}

AssetPaths AssetPaths::detect(const path &startDir)
{
    AssetPaths out{};

    if (auto root = get_env_path("VKG_ASSET_ROOT"); !root.empty())
    {
        out.root = root;
        if (std::filesystem::exists(root / "assets")) out.assets = root / "assets";
        if (std::filesystem::exists(root / "shaders")) out.shaders = root / "shaders";
        return out;
    }

    if (auto aroot = find_upwards_containing(startDir, "assets"); !aroot.empty())
    {
        out.assets = aroot / "assets";
        out.root = aroot;
    }
    if (auto sroot = find_upwards_containing(startDir, "shaders"); !sroot.empty())
    {
        out.shaders = sroot / "shaders";
        if (out.root.empty()) out.root = sroot;
    }

    if (out.assets.empty())
    {
        path p1 = startDir / "assets";
        path p2 = startDir / ".." / "assets";
        if (std::filesystem::exists(p1)) out.assets = p1;
        else if (std::filesystem::exists(p2)) out.assets = std::filesystem::weakly_canonical(p2);
    }
    if (out.shaders.empty())
    {
        path p1 = startDir / "shaders";
        path p2 = startDir / ".." / "shaders";
        if (std::filesystem::exists(p1)) out.shaders = p1;
        else if (std::filesystem::exists(p2)) out.shaders = std::filesystem::weakly_canonical(p2);
    }

    return out;
}

void AssetManager::init(VulkanEngine *engine)
{
    _engine = engine;
    _paths = AssetPaths::detect();
}

void AssetManager::cleanup()
{
    if (_engine && _engine->_resourceManager)
    {
        for (auto &kv: _meshCache)
        {
            if (kv.second)
            {
                _engine->_resourceManager->destroy_buffer(kv.second->meshBuffers.indexBuffer);
                _engine->_resourceManager->destroy_buffer(kv.second->meshBuffers.vertexBuffer);
            }
        }
        for (auto &kv: _meshMaterialBuffers)
        {
            _engine->_resourceManager->destroy_buffer(kv.second);
        }
        for (auto &kv: _meshOwnedImages)
        {
            for (const auto &img: kv.second)
            {
                _engine->_resourceManager->destroy_image(img);
            }
        }
    }
    _meshCache.clear();
    _meshMaterialBuffers.clear();
    _meshOwnedImages.clear();
    _gltfCacheByPath.clear();
}

bool AssetManager::file_exists(const path &p)
{
    std::error_code ec;
    return !p.empty() && std::filesystem::exists(p, ec) && std::filesystem::is_regular_file(p, ec);
}

std::string AssetManager::resolve_in(const path &base, std::string_view name)
{
    if (name.empty()) return {};
    path in = base / std::string(name);
    if (file_exists(in)) return in.string();
    return {};
}

std::string AssetManager::shaderPath(std::string_view name) const
{
    if (name.empty()) return {};
    path np = std::string(name);

    if (np.is_absolute() && file_exists(np)) return np.string();
    if (file_exists(np)) return np.string();

    if (!_paths.shaders.empty())
    {
        if (auto r = resolve_in(_paths.shaders, name); !r.empty()) return r;
    }

    if (auto r = resolve_in(std::filesystem::current_path() / "shaders", name); !r.empty()) return r;
    if (auto r = resolve_in(std::filesystem::current_path() / ".." / "shaders", name); !r.empty()) return r;

    return np.string();
}

std::string AssetManager::assetPath(std::string_view name) const
{
    if (name.empty()) return {};
    path np = std::string(name);
    if (np.is_absolute() && file_exists(np)) return np.string();
    if (file_exists(np)) return np.string();

    if (!_paths.assets.empty())
    {
        if (auto r = resolve_in(_paths.assets, name); !r.empty()) return r;
    }

    if (auto r = resolve_in(std::filesystem::current_path() / "assets", name); !r.empty()) return r;
    if (auto r = resolve_in(std::filesystem::current_path() / ".." / "assets", name); !r.empty()) return r;

    return np.string();
}

std::string AssetManager::modelPath(std::string_view name) const
{
    return assetPath(name);
}

std::optional<std::shared_ptr<LoadedGLTF> > AssetManager::loadGLTF(std::string_view nameOrPath)
{
    if (!_engine) return {};
    if (nameOrPath.empty()) return {};

    std::string resolved = assetPath(nameOrPath);

    path keyPath = resolved;
    std::error_code ec;
    keyPath = std::filesystem::weakly_canonical(keyPath, ec);
    std::string key = (ec ? resolved : keyPath.string());

    if (auto it = _gltfCacheByPath.find(key); it != _gltfCacheByPath.end())
    {
        if (auto sp = it->second.lock()) return sp;
    }

    auto loaded = loadGltf(_engine, resolved);
    if (!loaded.has_value()) return {};
    _gltfCacheByPath[key] = loaded.value();
    return loaded;
}

std::shared_ptr<MeshAsset> AssetManager::getPrimitive(std::string_view name) const
{
    if (name.empty()) return {};
    auto findBy = [&](const std::string &key) -> std::shared_ptr<MeshAsset> {
        auto it = _meshCache.find(key);
        return (it != _meshCache.end()) ? it->second : nullptr;
    };

    if (name == std::string_view("cube") || name == std::string_view("Cube"))
    {
        if (auto m = findBy("cube")) return m;
        if (auto m = findBy("Cube")) return m;
        return {};
    }
    if (name == std::string_view("sphere") || name == std::string_view("Sphere"))
    {
        if (auto m = findBy("sphere")) return m;
        if (auto m = findBy("Sphere")) return m;
        return {};
    }
    return {};
}

static Bounds compute_bounds(std::span<Vertex> vertices)
{
    Bounds b{};
    if (vertices.empty())
    {
        b.origin = glm::vec3(0.0f);
        b.extents = glm::vec3(0.5f);
        b.sphereRadius = glm::length(b.extents);
        return b;
    }
    glm::vec3 minpos = vertices[0].position;
    glm::vec3 maxpos = vertices[0].position;
    for (const auto &v: vertices)
    {
        minpos = glm::min(minpos, v.position);
        maxpos = glm::max(maxpos, v.position);
    }
    b.origin = (maxpos + minpos) / 2.f;
    b.extents = (maxpos - minpos) / 2.f;
    b.sphereRadius = glm::length(b.extents);
    return b;
}

std::shared_ptr<MeshAsset> AssetManager::createMesh(const std::string &name,
                                                    std::span<Vertex> vertices,
                                                    std::span<uint32_t> indices,
                                                    std::shared_ptr<GLTFMaterial> material)
{
    if (!_engine || !_engine->_resourceManager) return {};
    if (name.empty()) return {};

    auto it = _meshCache.find(name);
    if (it != _meshCache.end()) return it->second;

    if (!material)
    {
        GLTFMetallic_Roughness::MaterialResources matResources{};
        matResources.colorImage = _engine->_whiteImage;
        matResources.colorSampler = _engine->_samplerManager->defaultLinear();
        matResources.metalRoughImage = _engine->_whiteImage;
        matResources.metalRoughSampler = _engine->_samplerManager->defaultLinear();

        AllocatedBuffer matBuffer = _engine->_resourceManager->create_buffer(
            sizeof(GLTFMetallic_Roughness::MaterialConstants),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(_engine->_deviceManager->allocator(), matBuffer.allocation, &allocInfo);
        auto *matConstants = (GLTFMetallic_Roughness::MaterialConstants *) allocInfo.pMappedData;
        *matConstants = {};
        matConstants->colorFactors = glm::vec4(1.0f);
        matResources.dataBuffer = matBuffer.buffer;
        matResources.dataBufferOffset = 0;

        auto defaultMaterial = std::make_shared<GLTFMaterial>();
        defaultMaterial->data = _engine->metalRoughMaterial.write_material(
            _engine->_deviceManager->device(), MaterialPass::MainColor,
            matResources, *_engine->_context->descriptors);

        material = defaultMaterial;
        _meshMaterialBuffers.emplace(name, matBuffer);
    }

    auto mesh = std::make_shared<MeshAsset>();
    mesh->name = name;
    mesh->meshBuffers = _engine->_resourceManager->uploadMesh(indices, vertices);

    GeoSurface surf{};
    surf.startIndex = 0;
    surf.count = (uint32_t) indices.size();
    surf.material = material;
    surf.bounds = compute_bounds(vertices);
    mesh->surfaces.push_back(surf);

    _meshCache.emplace(name, mesh);
    return mesh;
}

std::shared_ptr<MeshAsset> AssetManager::createCube(const std::string &name)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    primitives::buildCube(verts, inds);
    return createMesh(name, verts, inds);
}

std::shared_ptr<MeshAsset> AssetManager::createSphere(const std::string &name, int sectors, int stacks)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    primitives::buildSphere(verts, inds, sectors, stacks);
    return createMesh(name, verts, inds);
}

std::shared_ptr<MeshAsset> AssetManager::createTexturedMesh(const std::string &name,
                                                            std::span<Vertex> vertices,
                                                            std::span<uint32_t> indices,
                                                            std::string_view albedoImage,
                                                            bool srgb)
{
    if (!_engine || !_engine->_resourceManager) return {};
    if (name.empty()) return {};

    if (auto it = _meshCache.find(name); it != _meshCache.end())
    {
        return it->second;
    }

    AllocatedImage albedo{};
    bool createdAlbedo = false;
    if (!albedoImage.empty())
    {
        std::string resolved = assetPath(albedoImage);
        int w = 0, h = 0, comp = 0;
        stbi_uc *pixels = stbi_load(resolved.c_str(), &w, &h, &comp, 4);
        if (pixels && w > 0 && h > 0)
        {
            VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            albedo = _engine->_resourceManager->create_image(pixels,
                                                             VkExtent3D{
                                                                 static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1
                                                             },
                                                             fmt,
                                                             VK_IMAGE_USAGE_SAMPLED_BIT,
                                                             false);
            createdAlbedo = true;
        }
        if (pixels) stbi_image_free(pixels);
    }

    const AllocatedImage &albedoRef = createdAlbedo ? albedo : _engine->_errorCheckerboardImage;
    const AllocatedImage &mrRef = _engine->_whiteImage;

    AllocatedBuffer matBuffer = _engine->_resourceManager->create_buffer(
        sizeof(GLTFMetallic_Roughness::MaterialConstants),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(_engine->_deviceManager->allocator(), matBuffer.allocation, &allocInfo);
    auto *matConstants = (GLTFMetallic_Roughness::MaterialConstants *) allocInfo.pMappedData;
    *matConstants = {};
    matConstants->colorFactors = glm::vec4(1.0f);

    GLTFMetallic_Roughness::MaterialResources res{};
    res.colorImage = albedoRef;
    res.colorSampler = _engine->_samplerManager->defaultLinear();
    res.metalRoughImage = mrRef;
    res.metalRoughSampler = _engine->_samplerManager->defaultLinear();
    res.dataBuffer = matBuffer.buffer;
    res.dataBufferOffset = 0;

    auto mat = std::make_shared<GLTFMaterial>();
    mat->data = _engine->metalRoughMaterial.write_material(
        _engine->_deviceManager->device(), MaterialPass::MainColor,
        res, *_engine->_context->descriptors);

    auto mesh = createMesh(name, vertices, indices, mat);

    _meshMaterialBuffers.emplace(name, matBuffer);
    if (createdAlbedo)
    {
        _meshOwnedImages[name].push_back(albedo);
    }

    return mesh;
}

std::shared_ptr<MeshAsset> AssetManager::createTexturedMesh(const std::string &name,
                                                            std::span<Vertex> vertices,
                                                            std::span<uint32_t> indices,
                                                            const MaterialOptions &options)
{
    if (!_engine || !_engine->_resourceManager) return {};
    if (name.empty()) return {};

    if (auto it = _meshCache.find(name); it != _meshCache.end())
    {
        return it->second;
    }

    AllocatedImage albedo{};
    bool createdAlbedo = false;
    if (!options.albedoPath.empty())
    {
        std::string resolved = assetPath(options.albedoPath);
        int w = 0, h = 0, comp = 0;
        stbi_uc *pixels = stbi_load(resolved.c_str(), &w, &h, &comp, 4);
        if (pixels && w > 0 && h > 0)
        {
            VkFormat fmt = options.albedoSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            albedo = _engine->_resourceManager->create_image(pixels, VkExtent3D{(uint32_t) w, (uint32_t) h, 1}, fmt,
                                                             VK_IMAGE_USAGE_SAMPLED_BIT, false);
            createdAlbedo = true;
        }
        if (pixels) stbi_image_free(pixels);
    }

    AllocatedImage mr{};
    bool createdMR = false;
    if (!options.metalRoughPath.empty())
    {
        std::string resolved = assetPath(options.metalRoughPath);
        int w = 0, h = 0, comp = 0;
        stbi_uc *pixels = stbi_load(resolved.c_str(), &w, &h, &comp, 4);
        if (pixels && w > 0 && h > 0)
        {
            VkFormat fmt = options.metalRoughSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
            mr = _engine->_resourceManager->create_image(pixels, VkExtent3D{(uint32_t) w, (uint32_t) h, 1}, fmt,
                                                         VK_IMAGE_USAGE_SAMPLED_BIT, false);
            createdMR = true;
        }
        if (pixels) stbi_image_free(pixels);
    }

    const AllocatedImage &albedoRef = createdAlbedo ? albedo : _engine->_errorCheckerboardImage;
    const AllocatedImage &mrRef = createdMR ? mr : _engine->_whiteImage;

    AllocatedBuffer matBuffer = _engine->_resourceManager->create_buffer(
        sizeof(GLTFMetallic_Roughness::MaterialConstants),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(_engine->_deviceManager->allocator(), matBuffer.allocation, &allocInfo);
    auto *matConstants = (GLTFMetallic_Roughness::MaterialConstants *) allocInfo.pMappedData;
    *matConstants = options.constants; // allow user-provided constants
    if (matConstants->colorFactors == glm::vec4(0))
    {
        matConstants->colorFactors = glm::vec4(1.0f);
    }

    GLTFMetallic_Roughness::MaterialResources res{};
    res.colorImage = albedoRef;
    res.colorSampler = _engine->_samplerManager->defaultLinear();
    res.metalRoughImage = mrRef;
    res.metalRoughSampler = _engine->_samplerManager->defaultLinear();
    res.dataBuffer = matBuffer.buffer;
    res.dataBufferOffset = 0;

    auto mat = std::make_shared<GLTFMaterial>();
    mat->data = _engine->metalRoughMaterial.write_material(
        _engine->_deviceManager->device(), options.pass,
        res, *_engine->_context->descriptors);

    auto mesh = createMesh(name, vertices, indices, mat);
    _meshMaterialBuffers.emplace(name, matBuffer);
    if (createdAlbedo) _meshOwnedImages[name].push_back(albedo);
    if (createdMR) _meshOwnedImages[name].push_back(mr);
    return mesh;
}

std::shared_ptr<MeshAsset> AssetManager::createTexturedCube(const std::string &name,
                                                            std::string_view albedoImage,
                                                            bool srgb)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    primitives::buildCube(verts, inds);
    return createTexturedMesh(name, verts, inds, albedoImage, srgb);
}

std::shared_ptr<MeshAsset> AssetManager::createTexturedCube(const std::string &name,
                                                            const MaterialOptions &options)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    primitives::buildCube(verts, inds);
    return createTexturedMesh(name, verts, inds, options);
}

std::shared_ptr<MeshAsset> AssetManager::createTexturedSphere(const std::string &name,
                                                              std::string_view albedoImage,
                                                              int sectors, int stacks,
                                                              bool srgb)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    primitives::buildSphere(verts, inds, sectors, stacks);
    return createTexturedMesh(name, verts, inds, albedoImage, srgb);
}

std::shared_ptr<MeshAsset> AssetManager::createTexturedSphere(const std::string &name,
                                                              const MaterialOptions &options,
                                                              int sectors, int stacks)
{
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
    primitives::buildSphere(verts, inds, sectors, stacks);
    return createTexturedMesh(name, verts, inds, options);
}

std::shared_ptr<MeshAsset> AssetManager::getMesh(const std::string &name) const
{
    auto it = _meshCache.find(name);
    return (it != _meshCache.end()) ? it->second : nullptr;
}

bool AssetManager::removeMesh(const std::string &name)
{
    auto it = _meshCache.find(name);
    if (it == _meshCache.end()) return false;
    if (_engine && _engine->_resourceManager)
    {
        _engine->_resourceManager->destroy_buffer(it->second->meshBuffers.indexBuffer);
        _engine->_resourceManager->destroy_buffer(it->second->meshBuffers.vertexBuffer);
    }
    _meshCache.erase(it);
    auto itb = _meshMaterialBuffers.find(name);
    if (itb != _meshMaterialBuffers.end())
    {
        if (_engine && _engine->_resourceManager)
        {
            _engine->_resourceManager->destroy_buffer(itb->second);
        }
        _meshMaterialBuffers.erase(itb);
    }
    auto iti = _meshOwnedImages.find(name);
    if (iti != _meshOwnedImages.end())
    {
        if (_engine && _engine->_resourceManager)
        {
            for (const auto &img: iti->second)
            {
                _engine->_resourceManager->destroy_image(img);
            }
        }
        _meshOwnedImages.erase(iti);
    }
    return true;
}

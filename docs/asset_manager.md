Asset Manager
=============

Overview
--------
- Centralizes asset path resolution and loading for models, shaders, and primitives.
- Avoids scattered relative paths by detecting project roots at runtime.

Path Resolution
---------------
- Honors environment variable `VKG_ASSET_ROOT` when set. Expected to contain `assets/` and `shaders/`.
- Otherwise searches upward from the current working directory for folders named `assets` and `shaders`.
- Falls back to `./assets`, `../assets` and `./shaders`, `../shaders`.

API Highlights
--------------
- `std::string shaderPath(name)`: Resolves SPIR-V shader path.
- `std::string modelPath(name)`: Resolves model path under `assets/`.
- `std::optional<std::shared_ptr<LoadedGLTF>> loadGLTF(nameOrPath)`: Loads and caches glTF by resolved absolute path.
- `std::shared_ptr<MeshAsset> createMesh(name, vertices, indices, [material])`: Uploads custom geometry and returns a mesh.
- `std::shared_ptr<MeshAsset> createCube(name)` / `createSphere(name, sectors, stacks)`: Convenience generators.
- `bool removeMesh(name)`: Destroys a runtime-created mesh and its owned resources.
- `std::shared_ptr<MeshAsset> createTexturedMesh(name, vertices, indices, albedoPath, [srgb=false])`: Simple albedo-only helper, owns image + UBO.
- `std::shared_ptr<MeshAsset> createTexturedCube(name, albedoPath, [srgb=false])` / `createTexturedSphere(name, albedoPath, sectors, stacks, [srgb=false])`: Albedo-only one-liners.
- Flexible options: `createTexturedMesh(name, vertices, indices, MaterialOptions)` and primitive variants to supply albedo, metal-rough textures, constants, and material pass.

Integration
-----------
- `VulkanEngine` creates and owns `AssetManager` and publishes it via `EngineContext::assets`.
- Shader and model paths in the engine now resolve through `AssetManager` (materials, passes, and scene loads).

Runtime Object Creation
-----------------------
You can create meshes and glTF scenes during rendering and insert them into the current frame via `SceneManager`.

Prerequisites (available from `EngineContext* ctx`):
- `ctx->getAssets()` → `AssetManager`
- `ctx->scene` → `SceneManager`
- `ctx->getResources()` → `ResourceManager` (for optional custom material buffers or textures)

Example: Create a simple plane primitive and render it
-----------------------------------------------------
```c++
// Build vertices/indices for a unit plane (XZ)
std::vector<Vertex> v = {
    { {-0.5f, 0.0f, -0.5f}, 0.0f, {0,1,0}, 0.0f, {1,1,1,1} },
    { { 0.5f, 0.0f, -0.5f}, 1.0f, {0,1,0}, 0.0f, {1,1,1,1} },
    { {-0.5f, 0.0f,  0.5f}, 0.0f, {0,1,0}, 1.0f, {1,1,1,1} },
    { { 0.5f, 0.0f,  0.5f}, 1.0f, {0,1,0}, 1.0f, {1,1,1,1} },
};
std::vector<uint32_t> i = { 0,1,2, 2,1,3 };

auto plane = ctx->getAssets()->createMesh("plane", v, i); // default white material
glm::mat4 xform = glm::scale(glm::mat4(1.f), glm::vec3(10.f, 1.f, 10.f));
ctx->scene->addMeshInstance("ground", plane, xform);
```

Example: Generate built-in primitives
-------------------------------------
```c++
auto cube = ctx->getAssets()->createCube("cubeA");
ctx->scene->addMeshInstance("cube.instance", cube,
                            glm::translate(glm::mat4(1.f), glm::vec3(-2.f, 0.f, -2.f)));

auto sphere = ctx->getAssets()->createSphere("sphere48x24", 48, 24);
ctx->scene->addMeshInstance("sphere.instance", sphere,
                            glm::translate(glm::mat4(1.f), glm::vec3(2.f, 0.f, -2.f)));
```

Example: Simple textured primitive (one-liner)
---------------------------------------------
```c++
// Build vertices/indices (see plane example above)
auto texturedPlane = ctx->getAssets()->createTexturedMesh(
    "ground.textured",
    std::span<Vertex>(v.data(), v.size()),
    std::span<uint32_t>(i.data(), i.size()),
    "textures/ground_albedo.png" // resolved via AssetManager
);
glm::mat4 xform = glm::scale(glm::mat4(1.f), glm::vec3(10.f, 1.f, 10.f));
ctx->scene->addMeshInstance("ground.textured", texturedPlane, xform);
```

Example: Textured cube and sphere
---------------------------------
```c++
auto crate = ctx->getAssets()->createTexturedCube("crate", "textures/crate_albedo.png");
ctx->scene->addMeshInstance("crate.instance", crate,
    glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.5f, -3.f)));

auto earth = ctx->getAssets()->createTexturedSphere("earth", "textures/earth_diffuse.png", 64, 32);
ctx->scene->addMeshInstance("earth.instance", earth,
    glm::translate(glm::mat4(1.f), glm::vec3(2.f, 0.5f, -3.f)));
```

Advanced: Manual material wiring
--------------------------------
The manual steps shown previously are still valid if you need full control over material buffers, samplers, or multiple textures. The helpers above cover the common case of “albedo only + default metal-roughness”.

Runtime glTF Spawning
---------------------
Load and place glTF scenes with transforms at runtime using `SceneManager::addGLTFInstance`.

```c++
auto chair = ctx->getAssets()->loadGLTF("models/chair.glb");
if (chair)
{
    glm::mat4 t = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -3.f));
    ctx->scene->addGLTFInstance("chair01", *chair, t);
}

// Move or overwrite transform
ctx->scene->addGLTFInstance("chair01", *chair, glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.5f, -3.f)));

// Remove
ctx->scene->removeGLTFInstance("chair01");
```

Example: Advanced textured material (albedo + metal-rough, custom pass)
-----------------------------------------------------------------------
```c++
AssetManager::MaterialOptions opts{};
opts.albedoPath = "textures/wood_albedo.png";         // sRGB on by default
opts.metalRoughPath = "textures/wood_mr.png";        // UNORM by default
opts.pass = MaterialPass::MainColor;                  // or MaterialPass::Transparent
opts.constants.colorFactors = glm::vec4(1,1,1,1);     // optional tweak
opts.constants.metal_rough_factors = glm::vec4(0,1,0,0); // mtl=0, rough=1 as example

auto woodCube = ctx->getAssets()->createTexturedCube("woodCube", opts);
ctx->scene->addMeshInstance("woodCube", woodCube,
    glm::translate(glm::mat4(1.f), glm::vec3(-1.f, 0.5f, -3.f)));
```

Notes
-----
- Meshes created through `AssetManager` are released on `AssetManager::cleanup()` or when you call `removeMesh(name)` explicitly.
- `SceneManager` draws all registered mesh and glTF instances each frame; overwriting with the same name updates the transform.
- By default, a unit white material is used when no material is provided to `createMesh()`.
- Textured helpers own their GPU resources (albedo/metal-rough images and material UBO). Use a unique `name`; if a mesh with that name already exists, it is reused and no new material is created.
- The flexible `MaterialOptions` currently wires albedo and metal-rough maps into the default GLTF PBR pipeline. Normal maps would require shader and descriptor updates and are not wired yet.

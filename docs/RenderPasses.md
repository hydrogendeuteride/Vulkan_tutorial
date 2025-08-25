## Render Passes: Background → Geometry → Lighting → ImGui

Modular pass system built on dynamic rendering. `RenderPassManager` sequences standalone passes that read/write shared images via `EngineContext`.

### Overview

- Interface: Each pass implements `IRenderPass { init(context); execute(cmd); cleanup(); getName(); }`.
- Manager: `RenderPassManager::init()` creates and registers built-in passes: `BackgroundPass` (compute), `GeometryPass` (G-Buffer), `LightingPass` (deferred), plus optional `ImGuiPass`.
- Dynamic rendering: Passes begin/end rendering with `vkCmdBeginRendering/EndRendering` and manage image layout transitions explicitly.
- Shared targets: Passes coordinate through `SwapchainManager` images: `drawImage`, `gBufferPosition/Normal/Albedo`, `depthImage`.
- Hot reload: Passes that use graphics pipelines should re-fetch handles each frame through `PipelineManager` to pick up shader changes.

### Quick Start — Add a New Pass

```c++
class MyPass : public IRenderPass {
public:
  void init(EngineContext* context) override {
    _ctx = context;
    // Build pipeline
    GraphicsPipelineCreateInfo info{};
    info.vertexShaderPath   = "../shaders/fullscreen.vert.spv";
    info.fragmentShaderPath = "../shaders/my_pass.frag.spv";
    info.setLayouts = { _ctx->getDescriptorLayouts()->gpuSceneDataLayout() };
    info.configure = [this](PipelineBuilder& b){
      b.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
      b.set_polygon_mode(VK_POLYGON_MODE_FILL);
      b.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
      b.set_multisampling_none(); b.disable_depthtest();
      b.set_color_attachment_format(_ctx->getSwapchain()->drawImage().imageFormat);
    };
    _ctx->pipelines->createGraphicsPipeline("my_pass", info);
  }

  void execute(VkCommandBuffer cmd) override {
    // Ensure target is in the right layout
    vkutil::transition_image(cmd, _ctx->getSwapchain()->drawImage().image,
                             VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Acquire pipeline/layout each frame for hot reload
    VkPipeline p{}; VkPipelineLayout l{};
    _ctx->pipelines->getGraphics("my_pass", p, l);

    // Dynamic rendering begin
    VkRenderingAttachmentInfo color = vkinit::attachment_info(
      _ctx->getSwapchain()->drawImage().imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo ri = vkinit::rendering_info(_ctx->getDrawExtent(), &color, nullptr);
    vkCmdBeginRendering(cmd, &ri);

    // Bind + draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    VkViewport vp{0,0,(float)_ctx->getDrawExtent().width,(float)_ctx->getDrawExtent().height,0,1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0,0},{_ctx->getDrawExtent().width,_ctx->getDrawExtent().height}}; vkCmdSetScissor(cmd,0,1,&sc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
  }

  void cleanup() override { /* destroy pass-owned layouts/sets if any */ }
  const char* getName() const override { return "MyPass"; }
private: EngineContext* _ctx{}; };

// Register in RenderPassManager::init()
auto myPass = std::make_unique<MyPass>(); myPass->init(context); addPass(std::move(myPass));
```

### Built-in Passes

- Background (compute): Writes directly into `drawImage` via `ComputeManager` instances. See `BackgroundPass::init_background_pipelines()` and `dispatchComputeInstance()`.
- Geometry (G-Buffer): Renders scene to three color attachments and depth. Sorts by material/index to reduce binds and updates `EngineStats`.
- Lighting (deferred): Fullscreen pass reading G-Buffer as sampled images, writing to `drawImage`. Pipeline built through `PipelineManager`.
- ImGui: Rendered after copying `drawImage` into the current swapchain image, drawn on top.

### API Summary

- `RenderPassManager::addPass(unique_ptr<IRenderPass>)`: Register a new pass.
- `RenderPassManager::executeAll(cmd)`: Execute in insertion order.
- `RenderPassManager::setImGuiPass(...)` / `executeImGui(...)`: Configure and render ImGui.
- `IRenderPass`: Implement `init/execute/cleanup/getName`.

### Tips

- Handle image layout transitions explicitly for every image you read/write in a pass.
- Re-fetch graphics pipeline and layout by key each frame to pick up hot-reloaded shaders.
- Allocate transient descriptor sets from `currentFrame->_frameDescriptors`; free pass-owned layouts in `cleanup()`.
- Use `EngineContext::getDrawExtent()` for dynamic viewport/scissor.


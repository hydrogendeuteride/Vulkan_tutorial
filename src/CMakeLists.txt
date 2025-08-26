
# Add source to this project's executable.
add_executable (engine 
  main.cpp
  # core
  core/vk_types.h
  core/vk_initializers.cpp
  core/vk_initializers.h
  core/vk_images.h
  core/vk_images.cpp 
  core/vk_descriptors.h
  core/vk_descriptors.cpp
  core/vk_device.h
  core/vk_device.cpp
  core/vk_swapchain.h
  core/vk_swapchain.cpp
  core/vk_resource.h
  core/vk_resource.cpp
  core/engine_context.h
  core/engine_context.cpp
  core/vk_descriptor_manager.h
  core/vk_descriptor_manager.cpp
  core/vk_sampler_manager.h
  core/vk_sampler_manager.cpp
  core/asset_locator.h
  core/asset_locator.cpp
  core/asset_manager.h
  core/asset_manager.cpp
  core/vk_pipeline_manager.h
  core/vk_pipeline_manager.cpp
  core/frame_resources.h
  core/frame_resources.cpp
  core/config.h
  core/vk_engine.h
  core/vk_engine.cpp
  # render
  render/vk_pipelines.h
  render/vk_pipelines.cpp
  render/vk_renderpass.h
  render/vk_renderpass.cpp
  render/vk_renderpass_background.h
  render/vk_renderpass_background.cpp
  render/vk_renderpass_geometry.h
  render/vk_renderpass_geometry.cpp
  render/vk_renderpass_lighting.h
  render/vk_renderpass_lighting.cpp
  render/vk_renderpass_imgui.h
  render/vk_renderpass_imgui.cpp
  render/vk_materials.h
  render/vk_materials.cpp
  render/primitives.h
  # scene
  scene/vk_scene.h
  scene/vk_scene.cpp
  scene/vk_loader.h
  scene/vk_loader.cpp
  scene/camera.h
  scene/camera.cpp
  # compute
  compute/vk_compute.h
  compute/vk_compute.cpp
)

set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(engine PUBLIC 
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/render"
  "${CMAKE_CURRENT_SOURCE_DIR}/scene"
  "${CMAKE_CURRENT_SOURCE_DIR}/compute"
)

target_link_libraries(engine PUBLIC vma glm Vulkan::Vulkan fmt::fmt stb_image SDL2::SDL2 vkbootstrap imgui fastgltf::fastgltf)

add_library(vma_impl OBJECT vma_impl.cpp)
target_include_directories(vma_impl PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vma")
target_link_libraries(vma_impl PRIVATE Vulkan::Vulkan)
target_link_libraries(engine PUBLIC $<TARGET_OBJECTS:vma_impl>)

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <vulkan/vulkan.h>)

add_custom_command(TARGET engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
  COMMAND_EXPAND_LISTS
  )

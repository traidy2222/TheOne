add_library(one_ui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(one_ui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(one_ui PUBLIC SDL3::SDL3-static Vulkan::Vulkan)
# Backend translation units are private; the public Renderer header is Vulkan-free.
add_library(one_renderer STATIC
  renderer.cpp
  vulkan/volume_resources.cpp
  vulkan/volume_runtime.cpp
  vulkan/hdr_pass.cpp
  spatial_regions.cpp
  vulkan/asset_owners.cpp
  vulkan/backend.cpp
  vulkan/asset_sync.cpp
  vulkan/asset_cache_invalidation.cpp
  vulkan/device.cpp
  vulkan/draw.cpp
  vulkan/draw_list.cpp
  vulkan/draw_preparation.cpp
  vulkan/frame.cpp
  vulkan/frame_resources.cpp
  vulkan/frame_profile.cpp
  vulkan/frame_trace.cpp
  vulkan/cpu_profiling.cpp
  vulkan/presentation_pacing.cpp
  vulkan/presentation_completion.cpp
  vulkan/visibility.cpp
  vulkan/instance_batches.cpp
  vulkan/asset_retirement.cpp
  vulkan/asset_invalidation.cpp
  vulkan/asset_memory.cpp
  vulkan/asset_memory_geometry.cpp
  vulkan/asset_memory_snapshot.cpp
  vulkan/geometry_cache.cpp
  vulkan/geometry_resources.cpp
  vulkan/lifecycle.cpp
  vulkan/materials.cpp
  vulkan/texture_alias_index.cpp
  vulkan/material_warmup.cpp
  vulkan/textures.cpp
  vulkan/texture_residency.cpp
  vulkan/texture_demand.cpp
  vulkan/material_residency.cpp
  vulkan/asset_preparation.cpp
  vulkan/texture_upload.cpp
  vulkan/staging_pool.cpp
  vulkan/upload_queue.cpp
  vulkan/upload_scheduler.cpp
  vulkan/material_streaming.cpp
  vulkan/memory.cpp
  vulkan/passes.cpp
  vulkan/pipelines.cpp
  vulkan/readback.cpp
  vulkan/render_targets.cpp
  vulkan/render_surface.cpp
  vulkan/resources.cpp
  vulkan/shadow_resources.cpp
  vulkan/shadow_mask.cpp
  vulkan/shadow_pass.cpp
  vulkan/shadow_submission.cpp
  vulkan/shadow_plan_cache.cpp
  vulkan/shadow_lod.cpp
  vulkan/swapchain.cpp
)
target_include_directories(one_renderer PRIVATE "${THEONE_SOURCE_DIR}/src")
target_link_libraries(one_renderer PUBLIC one_core one_ui PRIVATE one_geometry one_volumes)


add_library(one_scene STATIC
  picking.cpp
  picking_index.cpp
  world.cpp
  serialization.cpp
  asset_loading.cpp
  render_scene.cpp
  transform_cache.cpp
  asset_reload.cpp
)
target_include_directories(one_scene PRIVATE "${THEONE_SOURCE_DIR}/src")
target_link_libraries(one_scene PUBLIC one_assets PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(one_scene PRIVATE /W4 /permissive- /EHsc)
endif()

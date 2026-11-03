add_library(one_physics STATIC
  model_owners.cpp
  world.cpp
  body_shape.cpp
  edit_prepare.cpp
  edit_apply.cpp
  shape_cache.cpp
  body_state.cpp
  runtime.cpp
  scene_loading.cpp scene_reuse.cpp
  simulation.cpp
  character.cpp
  motion.cpp
)
target_include_directories(one_physics PRIVATE "${THEONE_SOURCE_DIR}/src")
target_link_libraries(one_physics PUBLIC one_scene PRIVATE Jolt)
if(MSVC)
  target_compile_options(one_physics PRIVATE /W4 /permissive- /EHsc)
endif()

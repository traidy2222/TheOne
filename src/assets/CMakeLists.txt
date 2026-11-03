add_library(one_assets STATIC
  cache/model_archive.cpp
  cache/prepared_cache.cpp
  cache/mapped_file.cpp
  cache/dependency_manifest.cpp
  model.cpp
  image.cpp
  cpu_geometry.cpp
  gltf/loader.cpp
  gltf/images.cpp
  gltf/image_decode.cpp
  gltf/materials.cpp
  gltf/meshes.cpp
  gltf/rig.cpp
  gltf/animations.cpp
)
target_include_directories(one_assets PRIVATE "${THEONE_SOURCE_DIR}/src" ${cgltf_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(one_assets PUBLIC one_animation one_geometry)
if(MSVC)
  target_compile_options(one_assets PRIVATE /W4 /permissive- /EHsc)
endif()
target_compile_definitions(one_assets PRIVATE _CRT_SECURE_NO_WARNINGS)

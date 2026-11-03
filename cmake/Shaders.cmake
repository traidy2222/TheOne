foreach(SHADER scene.vert scene.frag shadow.vert mesh.vert mesh_shadow.vert shadow_mask.vert mesh_shadow_mask.vert shadow_mask.frag scene_hdr.frag scene_hdr.vert mesh_hdr.vert volume_sim.comp volume_pressure.comp volume_light.comp volume_fullscreen.vert volume_resolve.frag volume_output.frag stellar_sky.vert stellar_sky.frag)
  set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
  add_custom_command(OUTPUT "${OUTPUT}" COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" -o "${OUTPUT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinning.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_uniform.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/cascades.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/volume_common.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/stellar.glsl" VERBATIM)
  list(APPEND SHADERS "${OUTPUT}")
endforeach()
add_custom_target(theone_shaders DEPENDS ${SHADERS})
set_property(GLOBAL PROPERTY THEONE_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
function(theone_copy_shaders TARGET)
  get_property(shader_directory GLOBAL PROPERTY THEONE_SHADER_DIRECTORY)
  # Deploy on every target build, including shader-only edits under Visual Studio.
  add_custom_target(${TARGET}_shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${shader_directory}" "$<TARGET_FILE_DIR:${TARGET}>/shaders"
    DEPENDS theone_shaders VERBATIM)
  add_dependencies(${TARGET} ${TARGET}_shaders)
endfunction()

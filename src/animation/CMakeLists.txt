add_library(one_animation STATIC
  pose.cpp
)
target_include_directories(one_animation PRIVATE "${THEONE_SOURCE_DIR}/src")
target_link_libraries(one_animation PUBLIC one_core)
if(MSVC)
  target_compile_options(one_animation PRIVATE /W4 /permissive- /EHsc)
endif()

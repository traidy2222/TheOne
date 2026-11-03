add_library(one_effects STATIC
  particles.cpp
)
target_include_directories(one_effects PRIVATE "${THEONE_SOURCE_DIR}/src")
target_link_libraries(one_effects PUBLIC one_core)
if(MSVC)
  target_compile_options(one_effects PRIVATE /W4 /permissive- /EHsc)
endif()

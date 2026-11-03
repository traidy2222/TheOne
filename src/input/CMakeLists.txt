add_library(one_input STATIC
  bindings.cpp
)
target_include_directories(one_input PRIVATE "${THEONE_SOURCE_DIR}/src")
target_link_libraries(one_input PUBLIC one_core SDL3::SDL3-static PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(one_input PRIVATE /W4 /permissive- /EHsc)
endif()

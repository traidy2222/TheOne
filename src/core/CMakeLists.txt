add_library(one_core INTERFACE)
target_include_directories(one_core INTERFACE "${THEONE_SOURCE_DIR}/include" "${THEONE_SOURCE_DIR}/include/compat")
target_link_libraries(one_core INTERFACE glm::glm)

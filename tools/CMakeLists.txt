add_executable(one_inspect inspect/main.cpp)
target_link_libraries(one_inspect PRIVATE TheOne::Assets nlohmann_json::nlohmann_json)
add_executable(one_import import/main.cpp)
target_link_libraries(one_import PRIVATE TheOne::Assets assimp::assimp nlohmann_json::nlohmann_json)

set_target_properties(one_import one_inspect PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

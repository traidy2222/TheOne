add_library(one_volumes STATIC volume.cpp)
target_link_libraries(one_volumes PUBLIC one_core)
add_library(TheOne::Volumes ALIAS one_volumes)

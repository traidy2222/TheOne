add_library(one_lighting STATIC stellar.cpp)
target_link_libraries(one_lighting PUBLIC one_core)
add_library(TheOne::Lighting ALIAS one_lighting)

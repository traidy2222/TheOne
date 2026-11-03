add_library(one_geometry STATIC prepare.cpp lod.cpp cascades.cpp mipmaps.cpp image_prepare.cpp image_intern.cpp)
target_link_libraries(one_geometry PUBLIC one_core PRIVATE meshoptimizer)
add_library(TheOne::Geometry ALIAS one_geometry)

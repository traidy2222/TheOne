#pragma once
#include <cstddef>
namespace one {
// Accumulated CPU wall times; parse includes reading the glTF/GLB file.
struct ModelLoadStats {
    double parseMs = 0, buffersMs = 0, validationMs = 0, imagesMs = 0;
    double materialsMs = 0, meshesMs = 0, rigMs = 0, animationsMs = 0;
    double cacheMs = 0, cacheHashMs = 0, cacheReadMs = 0, cacheDecodeMs = 0;
    size_t models = 0, cacheHits = 0;
};
} // namespace one

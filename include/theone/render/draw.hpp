#pragma once
#include <theone/assets/mesh.hpp>
#include <theone/assets/material.hpp>
#include <memory>
#include <cstdint>
namespace one {
struct Model;
struct Draw {
    uint32_t first = 0, count = 0;
    const Material* material = nullptr;
    const Image* image = nullptr;
    uint64_t entity = 0;
    // GPU draws reference immutable model geometry; ordinary draws retain the CPU vertex API.
    const Primitive* geometry = nullptr;
    std::shared_ptr<const Model> owner;
    uint32_t palette = 0, lod = 0;
    Bounds worldBounds;
    uint32_t instances = 1;
    // Transient renderer result, recomputed for the current camera each frame.
    bool cameraVisible = true, materialReady = false;
    uint32_t resolvedGeometry = UINT32_MAX, resolvedMaterial = UINT32_MAX; // Transient renderer token, overwritten on submission.
    uint8_t shadowVisibility = 7;
};
} // namespace one

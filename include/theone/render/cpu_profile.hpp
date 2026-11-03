#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace one {
enum class CpuStage : size_t { Total, Copies, Assets, Geometry, Uniforms, Visibility, Materials,
    Residency, Uploads, DrawList, Batches, Shadow, Main, Memory, Count };
inline constexpr std::array cpuStageNames{"total", "copies", "assets", "geometry", "uniforms",
    "visibility", "materials", "residency", "uploads", "draw_list", "batches", "shadow", "main", "memory"};
struct CpuStageSample { double wallMs = 0, threadMs = 0; uint64_t cycles = 0; };
}

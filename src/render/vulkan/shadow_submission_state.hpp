#pragma once
#include <theone/render/draw.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <unordered_map>
#include <cstdlib>
#include <string_view>
#include "geometry_lifetime.hpp"
#include "shadow_plan_cache.hpp"
namespace one::vulkan {
struct ShadowBinding {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex = 0, index = 0;
    bool operator==(const ShadowBinding&) const = default;
};
struct ShadowBindingHash {
    size_t operator()(const ShadowBinding& key) const {
        return std::hash<VkBuffer>{}(key.buffer) ^ (std::hash<uint64_t>{}(key.vertex) << 1) ^
               (std::hash<uint64_t>{}(key.index) << 2);
    }
};
struct ShadowGroup {
    ShadowBinding binding;
    std::vector<VkDrawIndexedIndirectCommand> commands;
    VkDeviceSize offset = 0;
    size_t count = 0;
};
struct MeshGPU;
struct ShadowGeometryTemplate {
    MeshGPU* mesh = nullptr;
    const Model* owner = nullptr;
    struct Level {
        size_t group;
        VkDrawIndexedIndirectCommand command;
    };
    std::vector<Level> levels;
};
struct ShadowCommandTemplate {
    const Primitive* geometry = nullptr;
    std::shared_ptr<GeometryLifetime> lifetime;
    const ShadowGeometryTemplate* binding = nullptr;
    glm::mat3 linear{0};
    float density = -1;
    uint64_t entity = 0;
    uint32_t selectedLevel = 0;
};
struct ShadowCascadeSubmission {
    size_t comparedKeys = 0;
    bool planValid = false;
    bool recordPlanKeys = true;
    uint32_t planRetryFrames = 0;
    float planDensity = 0;
    std::vector<ShadowPlanKey> planKeys;
    ShadowPlanCounts planCounts;
    std::vector<ShadowCommandTemplate> templates;
    std::unordered_map<const Primitive*, ShadowGeometryTemplate> geometryTemplates;
    std::unordered_map<ShadowBinding, size_t, ShadowBindingHash> lookup;
    std::vector<ShadowGroup> groups;
    std::vector<uint32_t> procedural, masked;
};
struct ShadowSubmission {
    bool referencePlan = std::getenv("THEONE_REFERENCE_SHADOW_PLAN") != nullptr;
    // Snapshot-cost stress probe only: alternating rebuilds intentionally prevent
    // reuse and record more keys than the normal retry policy.
    bool alternatePlan = referencePlan && std::string_view(std::getenv("THEONE_REFERENCE_SHADOW_PLAN")) == "alternate";
    bool referenceBindings = std::getenv("THEONE_REFERENCE_SHADOW_BINDINGS") != nullptr;
    bool alternateBindings = referenceBindings && std::string_view(std::getenv("THEONE_REFERENCE_SHADOW_BINDINGS")).starts_with("alternate");
    bool evenBindings = referenceBindings && std::string_view(std::getenv("THEONE_REFERENCE_SHADOW_BINDINGS")) == "alternate-even";
    uint64_t revision=1;
    std::array<ShadowCascadeSubmission, 3> cascades;
};
} // namespace one::vulkan

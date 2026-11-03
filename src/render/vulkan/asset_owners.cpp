#include "asset_owner_state.hpp"
#include <unordered_set>
namespace one::vulkan {
void AssetOwners::update(const std::vector<Draw>& scene, const std::vector<Draw>& dynamic) {
    bool changed = sequence.size() != scene.size() + dynamic.size();
    size_t index = 0;
    if (!changed)
        for (const auto* list : {&scene, &dynamic}) {
            if (changed)
                break;
            for (const auto& draw : *list)
                if (sequence[index++] != draw.owner.get()) {
                    changed = true;
                    break;
                }
        }
    if (!changed)
        return;
    sequence.clear();
    unique.clear();
    std::unordered_set<const Model*> seen;
    for (const auto* list : {&scene, &dynamic})
        for (const auto& draw : *list) {
            sequence.push_back(draw.owner.get());
            if (draw.owner && seen.insert(draw.owner.get()).second)
                unique.push_back(draw.owner);
        }
}
} // namespace one::vulkan

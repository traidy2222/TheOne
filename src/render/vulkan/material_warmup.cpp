#include "backend.hpp"
namespace one {
void Renderer::Impl::warmResidentMaterials(const std::vector<Draw>& draws) {
    if (draws.empty() || referenceMaterialWarmup)
        return;
    auto started = std::chrono::steady_clock::now();
    // Only descriptor/uniform work for textures already resident and ready.
    // No hidden-view image uploads, decode jobs, or texture-detail promotions.
    for (size_t checked = 0; checked < std::min<size_t>(64, draws.size()); ++checked) {
        auto& draw = draws[materialWarmupCursor++ % draws.size()];
        if (draw.cameraVisible || materialsState.materials.contains(draw.material))
            continue;
        Material fallback;
        const auto& m = draw.material ? *draw.material : fallback;
        const vulkan::TextureKey keys[] = {{draw.image, true},
                                           {m.metallicRoughness, false},
                                           {m.normal ? m.normal : &materialsState.flatNormal, false},
                                           {m.occlusion, false},
                                           {m.emissive, true}};
        bool ready = true;
        for (auto key : keys) {
            auto found = materialsState.textures.find(key);
            ready &= found != materialsState.textures.end() && found->second.upload->ready;
        }
        if (ready && materialAdmission.available() && prepareMaterial(draw))
            ++stats.materialWarmups;
        if (stats.materialWarmups >= 4 ||
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count() >=
                .25)
            break;
    }
}
} // namespace one

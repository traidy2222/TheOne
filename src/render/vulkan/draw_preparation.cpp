#include "backend.hpp"
namespace one {
void Renderer::Impl::prepareDrawMaterials(std::vector<Draw>& draws) {
    materialsState.references.resize(draws.size());
    for (size_t i = 0; i < draws.size(); ++i) {
        auto& draw = draws[i];
        draw.materialReady = false;
        draw.resolvedMaterial = UINT32_MAX;
        if (draw.cameraVisible) {
            auto& entry = materialsState.references[i];
            if (entry.source != draw.material || entry.image != draw.image ||
                entry.generation != materialsState.generation)
                entry = {draw.material, draw.image, nullptr, materialsState.generation};
            if (!entry.gpu || referenceDrawPreparation) {
                if (!prepareMaterial(draw))
                    continue;
                entry.gpu = &materialsState.materials.at(draw.material);
            }
            draw.materialReady = true;
            draw.resolvedMaterial = uint32_t(i);
        } else if (draw.shadowVisibility && draw.material && draw.material->mask && !draw.material->blend)
            prepareShadowMask(draw);
    }
}
const Renderer::Impl::MaterialGPU& Renderer::Impl::resolvedMaterial(const Draw& draw) {
    if (!referenceDrawPreparation && draw.resolvedMaterial < materialsState.references.size()) {
        auto& entry = materialsState.references[draw.resolvedMaterial];
        if (entry.source == draw.material && entry.image == draw.image &&
            entry.generation == materialsState.generation && entry.gpu) {
            if (entry.gpu->textureBindingsDirty[frameIndex])
                refreshMaterialTextures(*entry.gpu);
            return *entry.gpu;
        }
    }
    return material(draw.material, draw.image);
}
} // namespace one

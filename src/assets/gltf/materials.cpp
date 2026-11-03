#include "internal.hpp"
namespace one::gltf {
void loadMaterials(Model& model, cgltf_data* raw) {
    for (size_t i = 0; i < raw->materials_count; ++i) {
        const auto& input = raw->materials[i];
        Material m;
        m.name = name(input.name, "material");
        const auto& p = input.pbr_metallic_roughness;
        m.color = glm::make_vec4(p.base_color_factor);
        m.roughness = p.roughness_factor;
        m.metallic = p.metallic_factor;
        if (p.base_color_texture.texture && p.base_color_texture.texture->image)
            m.image = static_cast<int>(p.base_color_texture.texture->image - raw->images);
        m.blend = input.alpha_mode == cgltf_alpha_mode_blend;
        m.mask = input.alpha_mode == cgltf_alpha_mode_mask;
        m.cutoff = input.alpha_cutoff;
        if (p.base_color_texture.texcoord != 0)
            model.diagnostics.push_back(m.name + ": base color UV set is not TEXCOORD_0");
        auto imageFor = [&](const cgltf_texture_view& view) -> const Image* {
            if (view.texcoord != 0)
                model.diagnostics.push_back(m.name + ": secondary UV sets need conversion to TEXCOORD_0");
            if (view.has_transform)
                model.diagnostics.push_back(m.name + ": texture transforms must be baked into UVs");
            if (view.texture && view.texture->sampler)
                model.diagnostics.push_back(
                    m.name + ": source sampler is approximated by linear/repeat, without mipmaps");
            return view.texture && view.texture->image ? &model.images[view.texture->image - raw->images]
                                                       : nullptr;
        };
        m.metallicRoughness = imageFor(p.metallic_roughness_texture);
        m.normal = imageFor(input.normal_texture);
        m.occlusion = imageFor(input.occlusion_texture);
        m.emissive = imageFor(input.emissive_texture);
        imageFor(p.base_color_texture);
        m.emissiveFactor = glm::make_vec3(input.emissive_factor);
        m.normalScale = input.normal_texture.scale;
        m.occlusionStrength = input.occlusion_texture.scale;
        m.unlit = input.unlit != 0;
        if (m.mask)
            model.diagnostics.push_back(m.name +
                                        ": alpha-cutout shadows use solid geometry in this milestone");
        model.materials.push_back(m);
    }
}
} // namespace one::gltf

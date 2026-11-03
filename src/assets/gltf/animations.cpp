#include "internal.hpp"
namespace one::gltf {
void loadAnimations(Model& model, cgltf_data* raw, std::stop_token stop) {
    for (size_t i = 0; i < raw->animations_count; ++i) {
        const auto& a = raw->animations[i];
        Animation output;
        output.name = name(a.name, "clip");
        for (size_t j = 0; j < a.channels_count; ++j) {
            const auto& c = a.channels[j];
            if (!c.target_node)
                continue;
            if (c.target_path == cgltf_animation_path_type_weights) {
                model.diagnostics.push_back("Morph animation omitted");
                continue;
            }
            Channel ch;
            ch.node = static_cast<int>(c.target_node - raw->nodes);
            ch.path = c.target_path == cgltf_animation_path_type_translation ? 0
                      : c.target_path == cgltf_animation_path_type_rotation  ? 1
                                                                             : 2;
            ch.step = c.sampler->interpolation == cgltf_interpolation_type_step;
            ch.cubic = c.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
            need(c.sampler->input->count > 0 &&
                     c.sampler->output->count == c.sampler->input->count * (ch.cubic ? 3 : 1),
                 "Invalid animation key count");
            for (size_t k = 0; k < c.sampler->input->count; ++k) {
                if ((k & 4095) == 0)
                    checkCancellation(stop);
                float t = read(c.sampler->input, k).x;
                need(ch.times.empty() || t > ch.times.back(), "Animation key times must increase");
                ch.times.push_back(t);
                if (ch.cubic) {
                    ch.inTangents.push_back(read(c.sampler->output, k * 3));
                    ch.values.push_back(read(c.sampler->output, k * 3 + 1));
                    ch.outTangents.push_back(read(c.sampler->output, k * 3 + 2));
                } else
                    ch.values.push_back(read(c.sampler->output, k));
                output.duration = std::max(output.duration, t);
            }
            output.channels.push_back(std::move(ch));
        }
        model.animations.push_back(std::move(output));
    }
}
} // namespace one::gltf

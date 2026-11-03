#version 450
#extension GL_GOOGLE_include_directive : require
#include "volume_common.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
layout(push_constant) uniform Step {
    uint phase;
    uint state;
    uint pressure;
    uint srgb;
    float dt;
    float time;
    uint padding0;
    uint padding1;
}
pc;
vec3 outputCurve(vec3 x) {
    return clamp((x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14), 0, 1);
}
vec3 encode(vec3 x) {
    return mix(12.92 * x, 1.055 * pow(max(x, vec3(0)), vec3(1. / 2.4)) - .055, step(vec3(.0031308), x));
}
void main() {
    vec3 hdr = texture(resolvedColor, uv).rgb / HDR_STORAGE_SCALE;
    vec2 texel = 1. / vec2(textureSize(resolvedColor, 0));
    vec3 glow = vec3(0);
    for (int i = 0; i < 8; ++i) {
        float angle = 6.2831853 * (i + .5) / 8.;
        vec3 c =
            texture(resolvedColor, uv + vec2(cos(angle), sin(angle)) * texel * 5.).rgb / HDR_STORAGE_SCALE;
        glow += max(c - vec3(3), vec3(0)) / 8.;
    }
    vec3 mapped = outputCurve((hdr + glow * .04) * vol.ray.z);
    color = vec4(pc.srgb != 0 ? mapped : encode(mapped), 1);
}

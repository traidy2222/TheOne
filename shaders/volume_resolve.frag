#version 450
#extension GL_GOOGLE_include_directive : require
#include "volume_common.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main() {
    float depth = texture(opaqueDepth, uv).r;
    vec4 point = vol.inverseVP * vec4(uv * 2 - 1, depth, 1);
    vec3 world = point.xyz / point.w;
    vec3 delta = world - vol.eye.xyz;
    VolumeTransport v = integrateVolume(vol.eye.xyz, normalize(delta), length(delta));
    color = vec4(v.L * HDR_STORAGE_SCALE + v.T * texture(opaqueColor, uv).rgb, 1);
}

#version 450
#extension GL_GOOGLE_include_directive : require
#include "volume_common.glsl"
#include "stellar.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
void main() {
    vec4 farPoint = vol.inverseVP * vec4(uv * 2 - 1, 1, 1);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - vol.eye.xyz), color = vec3(0);
    float pixelArea = max(1e-14, length(cross(dFdx(direction), dFdy(direction))));
    float width = sqrt(pixelArea);
    for (int star = 0; star < int(vol.stellarCounts.x); ++star) {
        vec3 axis, tangent, bitangent;
        float oneMinusCos, omega;
        stellarCone(vol.eye.xyz, star, axis, tangent, bitangent, oneMinusCos, omega);
        if (dot(axis, direction) <= 0)
            continue;
        float angularRadius = asin(clamp(sqrt(oneMinusCos * (2 - oneMinusCos)), 0, 1));
        float separation = asin(clamp(length(cross(axis, direction)), 0, 1)), coverage;
        if (angularRadius > width * 1.5)
            coverage = 1 - smoothstep(angularRadius - width * .5, angularRadius + width * .5, separation);
        else {
            float variance = pixelArea * .5;
            coverage =
                min(1, omega / (2 * STELLAR_PI * variance) * exp(-.5 * separation * separation / variance));
        }
        float end = sphereEntry(vol.eye.xyz, axis, vol.starPositionRadius[star]);
        if (stellarBlocked(vol.eye.xyz, direction, max(0, end), -1))
            coverage = 0;
        color += vol.starRadiance[star].rgb * coverage;
    }
    outColor = vec4(min(vec3(60000), color * HDR_STORAGE_SCALE), 1);
}

const float STELLAR_PI = 3.141592653589793;
float sphereEntry(vec3 point, vec3 direction, vec4 sphere) {
    vec3 delta = sphere.xyz - point;
    float t = dot(delta, direction);
    vec3 perpendicular = cross(delta, direction);
    float discriminant = sphere.w * sphere.w - dot(perpendicular, perpendicular);
    if (discriminant < 0)
        return -1;
    float halfChord = sqrt(discriminant);
    return t + halfChord > 0 ? max(0, t - halfChord) : -1;
}
bool stellarBlocked(vec3 point, vec3 direction, float end, int excluded, int emitter) {
    for (int star = 0; star < int(vol.stellarCounts.x); ++star) {
        if (star == emitter)
            continue;
        float hit = sphereEntry(point, direction, vol.starPositionRadius[star]);
        if (hit >= 0 && hit < end)
            return true;
    }
    for (int j = 0; j < int(vol.stellarCounts.y); ++j) {
        if (j == excluded)
            continue;
        float hit = sphereEntry(point, direction, vol.occultingSpheres[j]);
        if (hit >= 0 && hit < end)
            return true;
    }
    return false;
}
void stellarCone(vec3 point, int star, out vec3 axis, out vec3 tangent, out vec3 bitangent,
                 out float oneMinusCos, out float omega) {
    vec3 delta = vol.starPositionRadius[star].xyz - point;
    float scale = max(abs(delta.x), max(abs(delta.y), abs(delta.z)));
    vec3 scaled = delta / max(scale, 1e-20);
    float lengthScaled = length(scaled);
    axis = scaled / max(lengthScaled, 1e-20);
    float ratio =
        clamp((vol.starPositionRadius[star].w / max(scale, 1e-20)) / max(lengthScaled, 1e-20), 0, 1);
    float cosine = sqrt(max(0, 1 - ratio * ratio));
    oneMinusCos = ratio * ratio / (1 + cosine);
    omega = 2 * STELLAR_PI * oneMinusCos;
    tangent = normalize(cross(axis, abs(axis.y) < .9 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    bitangent = cross(axis, tangent);
}
vec3 stellarDirection(vec3 axis, vec3 tangent, vec3 bitangent, float oneMinusCos, int sampleIndex) {
    float c = 1 - ((sampleIndex + .5) / 16.) * oneMinusCos;
    float angle = sampleIndex * 2.39996323;
    return axis * c + (tangent * cos(angle) + bitangent * sin(angle)) * sqrt(max(0, 1 - c * c));
}
vec3 stellarSurface(vec3 point, vec3 n, vec3 v, vec3 base, float rough, float metal, int excluded) {
    vec3 radiance = vec3(0);
    float nv = max(dot(n, v), .001), a = rough * rough, a2 = a * a, k = (rough + 1) * (rough + 1) / 8;
    vec3 F0 = mix(vec3(.04), base, metal);
    for (int star = 0; star < int(vol.stellarCounts.x); ++star) {
        vec3 axis, tangent, bitangent;
        float oneMinusCos, omega;
        stellarCone(point, star, axis, tangent, bitangent, oneMinusCos, omega);
        for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
            vec3 l = stellarDirection(axis, tangent, bitangent, oneMinusCos, sampleIndex);
            float nl = max(0, dot(n, l));
            if (nl <= 0)
                continue;
            float end = sphereEntry(point, l, vol.starPositionRadius[star]);
            if (end < 0 || stellarBlocked(point, l, end, excluded, star))
                continue;
            vec3 h = normalize(v + l);
            float nh = max(0, dot(n, h)), vh = max(0, dot(v, h));
            float den = nh * nh * (a2 - 1) + 1, D = a2 / (STELLAR_PI * den * den + .0001);
            float G = nv / (nv * (1 - k) + k) * nl / (nl * (1 - k) + k);
            vec3 F = F0 + (1 - F0) * pow(1 - vh, 5), specular = D * G * F / (4 * nv * max(nl, .001) + .0001);
            radiance += ((1 - F) * (1 - metal) * base / STELLAR_PI + specular) * nl *
                        vol.starRadiance[star].rgb * (omega / 16.);
        }
    }
    return radiance;
}

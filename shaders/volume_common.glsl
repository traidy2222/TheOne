#ifndef VOLUME_SET
#define VOLUME_SET 0
#endif
#ifdef VOLUME_WRITABLE
#define VOLUME_ACCESS
layout(constant_id = 2) const bool reuseConstrainedFaces = true;
#else
#define VOLUME_ACCESS readonly
#endif
struct VolumeCell {
    vec4 species;
    vec4 thermal;
    vec4 velocity;
};
layout(std430, set = VOLUME_SET, binding = 0) VOLUME_ACCESS buffer CellsA {
    VolumeCell cellsA[];
};
layout(std430, set = VOLUME_SET, binding = 1) VOLUME_ACCESS buffer CellsB {
    VolumeCell cellsB[];
};
layout(std430, set = VOLUME_SET, binding = 2) VOLUME_ACCESS buffer PressureA {
    float pressureA[];
};
layout(std430, set = VOLUME_SET, binding = 3) VOLUME_ACCESS buffer PressureB {
    float pressureB[];
};
layout(std430, set = VOLUME_SET, binding = 4) VOLUME_ACCESS buffer Divergence {
    float rhs[];
};
layout(std430, set = VOLUME_SET, binding = 5) VOLUME_ACCESS buffer Illumination {
    vec4 volumeLight[];
};
layout(std430, set = VOLUME_SET, binding = 7) VOLUME_ACCESS buffer Diagnostics {
    uint diagnostics[];
};
struct VolumeLedger {
    vec4 budget;
    vec4 species;
};
layout(std430, set = VOLUME_SET, binding = 12) VOLUME_ACCESS buffer Ledgers {
    VolumeLedger ledgers[];
};
layout(std140, set = VOLUME_SET, binding = 6) uniform VolumeParameters {
    mat4 inverseVP;
    vec4 eye, origin, extent;
    uvec4 grid;
    vec4 gas, gravityMax, windClosed, fuel, optics;
    vec4 sunDirection, sunRadiance, skyRadiance, ray;
    mat4 lightVP[3];
    vec4 splits, forward;
    uvec4 counts;
    vec4 initial;
    vec4 sourcePosition[16], sourceRates[16], sourceInterval[16], sourceVelocity[16];
    vec4 solidMin[16], solidMax[16], solidVelocity[16];
    vec4 stellarCounts;
    vec4 starPositionRadius[4],starRadiance[4];
    vec4 occultingSpheres[16];
}
vol;
layout(set = VOLUME_SET, binding = 8) uniform sampler2D opaqueDepth;
layout(set = VOLUME_SET, binding = 9) uniform sampler2D opaqueColor;
layout(set = VOLUME_SET, binding = 10) uniform sampler2DArrayShadow volumeShadow;
layout(set = VOLUME_SET, binding = 11) uniform sampler2D resolvedColor;
ivec3 dimensions() {
    return ivec3(vol.grid.xyz);
}
vec3 spacing() {
    return vol.extent.xyz / vec3(vol.grid.xyz);
}
bool outside(ivec3 i) {
    return any(lessThan(i, ivec3(0))) || any(greaterThanEqual(i, dimensions()));
}
uint cellIndex(ivec3 i) {
    return uint((i.z * dimensions().y + i.y) * dimensions().x + i.x);
}
vec3 cellPosition(ivec3 i) {
    return vol.origin.xyz + (vec3(i) + .5) * spacing();
}
VolumeCell ambientCell() {
    VolumeCell c;
    c.species = vec4(0, .233 * vol.gas.y, 0, 0);
    c.thermal = vec4(.767 * vol.gas.y, vol.gas.y * vol.gas.z * vol.gas.x, 0, 1000);
    c.velocity = vec4(vol.windClosed.xyz, 0);
    return c;
}
VolumeCell stateCell(ivec3 i, uint state) {
    if (outside(i))
        return ambientCell();
    return state == 0 ? cellsA[cellIndex(i)] : cellsB[cellIndex(i)];
}
#ifdef VOLUME_WRITABLE
void storeCell(ivec3 i, uint state, VolumeCell c) {
    if (state == 0)
        cellsA[cellIndex(i)] = c;
    else
        cellsB[cellIndex(i)] = c;
}
#endif
float density(VolumeCell c) {
    return max(dot(c.species, vec4(1)) + c.thermal.x, .0001);
}
float temperature(VolumeCell c) {
    return max(1., c.thermal.y / (density(c) * vol.gas.z));
}
bool solidAt(vec3 p) {
    if (p.y < vol.origin.y)
        return true;
    if (vol.windClosed.w > .5 &&
        (any(lessThan(p, vol.origin.xyz)) || any(greaterThan(p, vol.origin.xyz + vol.extent.xyz))))
        return true;
    for (uint b = 0; b < vol.counts.y; ++b)
        if (all(greaterThanEqual(p, vol.solidMin[b].xyz)) && all(lessThanEqual(p, vol.solidMax[b].xyz)))
            return true;
    return false;
}
bool solidCell(ivec3 i) {
    vec3 p = cellPosition(i);
    if (p.y < vol.origin.y)
        return true;
    if (vol.windClosed.w > .5 && outside(i))
        return true;
    vec3 halfCell = spacing() * .5;
    for (uint b = 0; b < vol.counts.y; ++b)
        if (all(greaterThanEqual(p + halfCell, vol.solidMin[b].xyz)) &&
            all(lessThanEqual(p - halfCell, vol.solidMax[b].xyz)))
            return true;
    return false;
}
vec3 boundaryVelocity(vec3 p) {
    for (uint b = 0; b < vol.counts.y; ++b)
        if (all(greaterThanEqual(p, vol.solidMin[b].xyz - spacing() * .51)) &&
            all(lessThanEqual(p, vol.solidMax[b].xyz + spacing() * .51)))
            return vol.solidVelocity[b].xyz;
    return vec3(0);
}
bool blockedFace(ivec3 i, int a) {
    ivec3 j = i;
    j[a]++;
    vec3 p = cellPosition(i);
    p[a] += spacing()[a] * .5;
    if (solidCell(i) || solidCell(j) || solidAt(p))
        return true;
    // Conservative voxelization of thin solids at face crossings.
    for (uint b = 0; b < vol.counts.y; ++b) {
        vec3 lo = vol.solidMin[b].xyz, hi = vol.solidMax[b].xyz;
        bool overlap = true;
        for (int k = 0; k < 3; ++k)
            overlap = overlap &&
                      (k == a ? (p[k] >= lo[k] && p[k] <= hi[k])
                              : (p[k] + spacing()[k] * .5 >= lo[k] && p[k] - spacing()[k] * .5 <= hi[k]));
        if (overlap)
            return true;
    }
    return false;
}
float faceVelocity(ivec3 i, int axis, uint state) {
#ifdef VOLUME_WRITABLE
    // Every velocity-producing pass enforces the face boundary before storing.
    // Outside/ghost faces still need the geometric boundary rule below.
    if (reuseConstrainedFaces && !outside(i))
        return stateCell(i, state).velocity[axis];
#endif
    if (blockedFace(i, axis)) {
        vec3 p = cellPosition(i);
        p[axis] += spacing()[axis] * .5;
        return boundaryVelocity(p)[axis];
    }
    return stateCell(i, state).velocity[axis];
}
vec3 sampleVelocity(vec3 world, uint state) {
    vec3 result = vec3(0), h = spacing();
    for (int a = 0; a < 3; ++a) {
        vec3 offset = vec3(.5);
        offset[a] = 1;
        vec3 x = (world - vol.origin.xyz) / h - offset;
        ivec3 base = ivec3(floor(x));
        vec3 f = fract(x);
        float value = 0;
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 2; ++y)
                for (int k = 0; k < 2; ++k) {
                    ivec3 o = ivec3(k, y, z);
                    vec3 w = mix(1 - f, f, vec3(o));
                    value += faceVelocity(base + o, a, state) * w.x * w.y * w.z;
                }
        result[a] = value;
    }
    return result;
}
bool rayBox(vec3 ro, vec3 rd, vec3 lo, vec3 hi, inout float nearT, inout float farT) {
    for (int a = 0; a < 3; ++a) {
        if (abs(rd[a]) < 1e-7) {
            if (ro[a] < lo[a] || ro[a] > hi[a])
                return false;
        } else {
            float x = (lo[a] - ro[a]) / rd[a], y = (hi[a] - ro[a]) / rd[a];
            nearT = max(nearT, min(x, y));
            farT = min(farT, max(x, y));
        }
    }
    return farT > nearT;
}
vec3 traceBack(vec3 p, vec3 q) {
    float limit = 1.;
    for (uint b = 0; b < vol.counts.y; ++b) {
        float n = 0, f = 1;
        if (rayBox(p, q - p, vol.solidMin[b].xyz - spacing() * .05, vol.solidMax[b].xyz + spacing() * .05, n,
                   f))
            limit = min(limit, max(0, n - .001));
    }
    q = mix(p, q, limit);
    return clamp(q, vol.origin.xyz + spacing() * .05, vol.origin.xyz + vol.extent.xyz - spacing() * .05);
}
VolumeCell sampleState(vec3 world) {
    vec3 x = (world - vol.origin.xyz) / spacing() - .5;
    ivec3 base = ivec3(floor(x));
    vec3 f = fract(x);
    VolumeCell r;
    r.species = vec4(0);
    r.thermal = vec4(0);
    r.velocity = vec4(0);
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int k = 0; k < 2; ++k) {
                ivec3 o = ivec3(k, y, z), i = base + o;
                vec3 w = mix(1 - f, f, vec3(o));
                float weight = w.x * w.y * w.z;
                i = clamp(i, ivec3(0), dimensions() - 1);
                if (!solidCell(i)) {
                    VolumeCell c = stateCell(i, vol.counts.z);
                    r.species += c.species * weight;
                    r.thermal += c.thermal * weight;
                }
            }
    return r;
}
vec4 sampleLighting(vec3 world) {
    vec3 x = (world - (vol.origin.xyz - vol.extent.xyz * .5)) / (spacing() * 2) - .5;
    ivec3 base = ivec3(floor(x));
    vec3 f = fract(x);
    vec4 r = vec4(0);
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int k = 0; k < 2; ++k) {
                ivec3 o = ivec3(k, y, z), i = base + o;
                vec3 w = mix(1 - f, f, vec3(o));
                r += (!outside(i)
                          ? volumeLight[cellIndex(i) + uint(dimensions().x * dimensions().y * dimensions().z)]
                          : vec4(0, 0, 0, 1)) *
                     w.x * w.y * w.z;
            }
    return r;
}
const float HDR_STORAGE_SCALE = 1. / 1024.;
vec3 thermalRadiance(float t) {
    // Planck radiance sampled in three visible bands (100 nm bandwidth each).
    // This is an RGB quadrature, not full spectral chemistry/chemiluminescence.
    vec3 lambda = vec3(650, 550, 450) * 1e-9;
    return 1.19104297e-16 /
           (pow(lambda, vec3(5)) * (exp(min(vec3(80), .01438777 / (lambda * clamp(t, 1., 6000.)))) - 1.)) *
           1e-7;
}
float phaseHG(float cosine) {
    float g = vol.optics.z;
    return (1 - g * g) / (12.5663706144 * pow(max(.001, 1 + g * g - 2 * g * cosine), 1.5));
}
float opaqueSunVisibility(vec3 p) {
    if (vol.ray.w < .5)
        return 1.;
    float depth = dot(p - vol.eye.xyz, vol.forward.xyz);
    int c = depth > vol.splits.y ? 2 : (depth > vol.splits.x ? 1 : 0);
    vec4 clip = vol.lightVP[c] * vec4(p, 1);
    vec3 q = clip.xyz / clip.w;
    vec2 uv = q.xy * .5 + .5;
    if (q.z <= 0 || q.z >= 1 || any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1))))
        return 1.;
    return texture(volumeShadow, vec4(uv, c, q.z - .0003));
}
vec3 smokeSigma(VolumeCell c) {
    return max(c.species.z, 0.) * vec3(vol.optics.x + vol.optics.y);
}
struct VolumeTransport {
    vec3 L;
    vec3 T;
};
VolumeTransport integrateVolume(vec3 ro, vec3 rd, float end) {
    VolumeTransport r;
    r.L = vec3(0);
    r.T = vec3(1);
    if (vol.grid.w == 0)
        return r;
    float lo = 0, hi = end;
    if (!rayBox(ro, rd, vol.origin.xyz, vol.origin.xyz + vol.extent.xyz, lo, hi))
        return r;
    float h = min(spacing().x, min(spacing().y, spacing().z)) * vol.ray.x;
    int count = min(int(vol.ray.y), max(1, int(ceil((hi - lo) / h))));
    float ds = (hi - lo) / count;
    float distance = lo;
    for (int n = 0; n < 1024; ++n) {
        if (n >= int(vol.ray.y) || distance >= hi || max(r.T.r, max(r.T.g, r.T.b)) < .001)
            break;
        ivec3 bricks = (dimensions() + 7) / 8;
        vec3 cell = (ro + rd * (distance + .00001) - vol.origin.xyz) / spacing();
        ivec3 brick = clamp(ivec3(floor(cell / 8)), ivec3(0), bricks - 1);
        uint brickIndex = uint((brick.z * bricks.y + brick.y) * bricks.x + brick.x);
        if (volumeLight[uint(dimensions().x * dimensions().y * dimensions().z) * 2 + brickIndex * 2].w <
            1e-10) {
            float entry = distance, exit = hi;
            vec3 bmin = vol.origin.xyz + vec3(brick * 8) * spacing();
            if (rayBox(ro, rd, bmin, min(bmin + spacing() * 8, vol.origin.xyz + vol.extent.xyz), entry,
                       exit)) {
                distance = max(distance + .0001, exit + .00001);
                continue;
            }
        }
        float segment = min(ds, hi - distance);
        vec3 p = ro + rd * (distance + segment * .5);
        distance += segment;
        VolumeCell c = sampleState(p);
        float soot = max(0., c.species.z);
        if (soot < 1e-8)
            continue;
        float sigmaA = soot * vol.optics.x, sigmaS = soot * vol.optics.y, sigma = sigmaA + sigmaS;
        vec4 light = sampleLighting(p);
        vec3 source = sigmaA * thermalRadiance(temperature(c)) +
                      sigmaS * (vol.sunRadiance.xyz * phaseHG(dot(rd, vol.sunDirection.xyz)) * light.w +
                                vol.skyRadiance.xyz + light.rgb / 12.5663706144);
        float tau = sigma * segment, tr = exp(-tau);
        float integral = tau < .001 ? segment * (1 - tau * .5 + tau * tau / 6) : (1 - tr) / sigma;
        r.L += r.T * source * integral;
        r.T *= tr;
    }
    return r;
}

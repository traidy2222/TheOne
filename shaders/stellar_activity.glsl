// Procedural photosphere/background plus an optional density/temperature MHD snapshot.
layout(std430, set = VOLUME_SET, binding = 13) readonly buffer StellarPlasma {
    vec4 plasmaCells[];
};
uint activityHash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
vec3 activityHash3(ivec3 p, uint seed) {
    uint h = activityHash(uint(p.x) * 1597334677u ^ uint(p.y) * 3812015801u ^ uint(p.z) * 2798796415u ^ seed);
    return vec3(activityHash(h), activityHash(h + 17u), activityHash(h + 93u)) / 4294967295.;
}
float activityAngle(vec3 p, int star) {
    float latitude = p.y / max(length(p), 1e-8);
    return -vol.starMotion[star].y * (1 - .2 * latitude * latitude);
}
vec3 activityRotate(vec3 p, float angle) {
    float s = sin(angle), c = cos(angle);
    return vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
}
vec3 activityFrame(vec3 p, int star) {
    return activityRotate(p, activityAngle(p, star));
}
vec2 activityCells(vec3 p, float phase, uint seed) {
    ivec3 base = ivec3(floor(p));
    vec3 fraction = fract(p);
    float first = 100, second = 100;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                ivec3 offset = ivec3(x, y, z);
                vec3 random = activityHash3(base + offset, seed);
                vec3 centre = vec3(offset) + .5 + .28 * sin(random * 6.2831853 + phase * vec3(6.3, 7.1, 5.7));
                float d = length(centre - fraction);
                if (d < first) {
                    second = first;
                    first = d;
                } else
                    second = min(second, d);
            }
    return vec2(first, second - first);
}
float activityNoise(vec3 p, uint seed) {
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);
    f = f * f * (3 - 2 * f);
    float result = 0;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                vec3 w = mix(1 - f, f, vec3(x, y, z));
                result += activityHash3(i + ivec3(x, y, z), seed).x * w.x * w.y * w.z;
            }
    return result;
}
vec3 activityPlanck(float t) {
    vec3 l = vec3(650, 550, 450) * 1e-9;
    return 1.19104297e-16 / (pow(l, vec3(5)) * (exp(min(vec3(80), .01438777 / (l * max(100., t)))) - 1)) *
           1e-7;
}
float activityLimb(float mu, int star) {
    float u = vol.starActivity[star].w;
    return (1 - u + u * clamp(mu, 0, 1)) / (1 - u / 3);
}
void activityRegions(vec3 normal, int star, out float spot, out float facula) {
    spot = 0;
    facula = 0;
    if (int(vol.plasmaNormal.w) == star + 1)
        return;
    for (int j = 0; j < 4; ++j) {
        int index = star * 4 + j;
        vec3 n = vol.starRegionNormal[index].xyz, u = vol.starRegionTangent[index].xyz;
        float a = vol.starRegionNormal[index].w;
        vec3 c = n * sqrt(1 - a * a);
        float width = a * .24;
        for (int side = -1; side <= 1; side += 2) {
            vec3 foot = c + u * a * side;
            float q = length(normal - foot) / width;
            spot += .2 * exp(-q * q * 2) + .09 * exp(-q * q * .35);
            facula += exp(-q * q * .12);
        }
    }
    spot *= vol.starMotion[star].z;
    facula *= vol.starMotion[star].z;
}
vec3 activityPhotosphere(vec3 normal, float mu, float footprint, int star) {
    vec3 n = activityFrame(normal, star);
    float phase = vol.starMotion[star].x;
    uint seed = uint(vol.starMotion[star].w);
    float resolution = footprint * vol.starActivity[star].y / max(.12, mu);
    float resolved = 1 - smoothstep(.35, 1.5, resolution);
    float fluctuation = 0;
    if (resolved > .005) {
        vec3 p = n * vol.starActivity[star].y;
        p +=
            .18 * vec3(sin(n.y * 27 + phase * .17), sin(n.z * 31 - phase * .13), sin(n.x * 29 + phase * .19));
        vec3 warp = vec3(activityNoise(p * .7, seed + 1u), activityNoise(p * .7 + 17, seed + 2u),
                         activityNoise(p * .7 - 23, seed + 3u)) -
                    .5;
        p += warp * .85;
        vec2 cells = activityCells(p, phase, seed);
        fluctuation = clamp(1.3 * exp(-cells.x * cells.x * 2.3) - .35 - .7 * exp(-cells.y * 24) +
                                .35 * (activityNoise(p * 4, seed + 4u) - .5),
                            -1, 1) *
                      resolved;
    }
    float spot, facula;
    activityRegions(n, star, spot, facula);
    float temperatureRatio =
        clamp(1 + vol.starActivity[star].z * fluctuation - spot + .004 * facula, .55, 1.2);
    vec3 local = activityPlanck(vol.starRadiance[star].w * temperatureRatio) /
                 max(vec3(1e-10), activityPlanck(vol.starRadiance[star].w));
    return vol.starRadiance[star].rgb * local * activityLimb(mu, star);
}
vec3 activityEuvSurface(vec3 normal, int star) {
    if (int(vol.plasmaNormal.w) == star + 1)
        return vec3(0);
    vec3 n = activityFrame(normal, star);
    float spot, facula;
    activityRegions(n, star, spot, facula);
    uint seed = uint(vol.starMotion[star].w);
    vec3 drift = vec3(vol.starMotion[star].x * .025, 0, 0);
    float moss = activityNoise(n * 67 + drift, seed + 317u);
    float fine = activityNoise(n * 173 - drift, seed + 718u);
    float activePatch = activityNoise(n * 13, seed + 51u);
    float brightness = .025 + .15 * pow(moss, 3) + .035 * fine + .22 * facula * (.4 + activePatch);
    float meanBand = dot(vol.starRadiance[star].rgb, vec3(.3333333));
    vec3 palette = int(vol.stellarCounts.w) == 3 ? vec3(1) : vec3(1, .36, .06);
    return meanBand * palette * brightness * .15;
}
struct ActivityField {
    float electrons;
    float hotMeasure;
    float coolMeasure;
};
bool hasPlasma(int star) {
    return int(vol.plasmaNormal.w) == star + 1;
}
float plasmaResponse(float temperature) {
    // Monochrome combines a broad range of coronal temperatures for inspection;
    // colour mode retains the narrower illustrative 1.5 MK response.
    float width = int(vol.stellarCounts.w) == 3 ? 1.8 : .48;
    return exp(-.5 * pow(log(max(temperature, 100.) / 1.5e6) / width, 2));
}
vec4 plasmaSample(vec2 coord) {
    vec2 pixel = clamp(coord * vol.plasmaGrid.xy - .5, vec2(0), vol.plasmaGrid.xy - 1);
    ivec2 a = ivec2(floor(pixel)), b = min(a + 1, ivec2(vol.plasmaGrid.xy) - 1);
    vec2 f = fract(pixel);
    int width = int(vol.plasmaGrid.x);
    return mix(mix(plasmaCells[a.y * width + a.x], plasmaCells[a.y * width + b.x], f.x),
               mix(plasmaCells[b.y * width + a.x], plasmaCells[b.y * width + b.x], f.x), f.y);
}
void plasmaField(vec3 p, inout ActivityField field) {
    float scale = vol.plasmaTangent.w;
    vec3 n = vol.plasmaNormal.xyz, u = vol.plasmaTangent.xyz, v = cross(n, u);
    vec2 q = vec2(atan(dot(p, u), dot(p, n)), length(p) - 1) / scale;
    vec2 coord = (q - vol.plasmaGrid.zw) / vol.plasmaExtent.xy;
    if (any(lessThan(coord, vec2(0))) || any(greaterThan(coord, vec2(1))))
        return;
    float depth = dot(p, v) / scale / vol.plasmaExtent.z;
    if (abs(depth) > 3.5)
        return;
    vec4 cell = plasmaSample(coord);
    float density = cell.x / 4e14, temperature = cell.y;
    float edge =
        smoothstep(0, .04, coord.x) * smoothstep(0, .04, 1 - coord.x) * smoothstep(0, .06, 1 - coord.y);
    float weight = exp(-.5 * depth * depth) * edge * (1 - smoothstep(3., 3.5, abs(depth)));
    // Synthetic optically thin coronal response, not a calibrated atomic line table.
    float response = plasmaResponse(temperature);
    float cool = 1 - smoothstep(6e4, 1.8e5, temperature);
    float ambient = cell.z / 4e14;
    float ambientResponse = plasmaResponse(cell.w);
    field.electrons = max(0, field.electrons + weight * (density - ambient));
    field.hotMeasure =
        max(0, field.hotMeasure +
                   weight * .45 * (density * density * response - ambient * ambient * ambientResponse));
    field.coolMeasure += density * density * cool * cool * weight;
}
// Gaussian cross sections convolved with pixel footprint and ray segment.
// Density and emission measure are filtered separately: <n^2> is not <n>^2.
vec2 activityTube(float planeDistance, float radialDistance, float width, float planeRay, float radialRay,
                  float footprint, float ds) {
    float pixelVariance = footprint * footprint / 12;
    float v1 = width * width + pixelVariance + ds * ds * planeRay * planeRay / 12;
    float v2 = width * width + pixelVariance + ds * ds * radialRay * radialRay / 12;
    if (abs(planeDistance) > sqrt(v1) * 4 || abs(radialDistance) > sqrt(v2) * 4)
        return vec2(0);
    float density = width * width * inversesqrt(v1 * v2) *
                    exp(-.5 * (planeDistance * planeDistance / v1 + radialDistance * radialDistance / v2));
    float e1 = width * width * .5 + pixelVariance + ds * ds * planeRay * planeRay / 12;
    float e2 = width * width * .5 + pixelVariance + ds * ds * radialRay * radialRay / 12;
    float measure = width * width * .5 * inversesqrt(e1 * e2) *
                    exp(-.5 * (planeDistance * planeDistance / e1 + radialDistance * radialDistance / e2));
    return vec2(density, measure);
}
ActivityField activityAtmosphere(vec3 point, vec3 ray, float footprint, float ds, int star,
                                 bool includePatch) {
    ActivityField field;
    float radius = length(point), height = radius - 1;
    vec3 p = activityFrame(point, star), n = p / max(radius, 1e-8);
    vec3 rotatedRay = activityRotate(ray, activityAngle(point, star));
    float taper = 1 - smoothstep(vol.starAtmosphere[star].z - .18, vol.starAtmosphere[star].z, radius);
    float quiet = pow(max(radius, 1), -6) * (.55 + .45 * (1 - n.y * n.y));
    uint seed = uint(vol.starMotion[star].w);
    // Mostly angular modulation: fine density structures stretch radially.
    if (!hasPlasma(star)) {
        float stream = activityNoise(n * 35 + vec3(0, height * .7, 0), seed + 172u);
        float fine = activityNoise(n * 93 + vec3(height * .3, 0, 0), seed + 91u);
        quiet *= .18 + 1.8 * stream * stream * (.5 + fine);
    }
    field.electrons = quiet;
    field.hotMeasure = .025 * quiet * quiet;
    field.coolMeasure = 0;
    if (hasPlasma(star)) {
        float y = (radius - 1) / vol.plasmaTangent.w;
        vec4 ambient = plasmaSample(vec2(.5, (y - vol.plasmaGrid.w) / vol.plasmaExtent.y));
        if (ambient.z > 0) {
            float top = 1 + (vol.plasmaGrid.w + vol.plasmaExtent.y) * vol.plasmaTangent.w;
            float density = ambient.z / 4e14 * pow(max(1., radius / top), -6);
            float response = plasmaResponse(ambient.w);
            field.electrons = density;
            field.hotMeasure = .45 * density * density * response;
        }
    }
    float strength = vol.starMotion[star].z, flow = vol.starAtmosphere[star].w;
    for (int j = 0; j < (hasPlasma(star) ? 0 : 4); ++j) {
        int index = star * 4 + j;
        vec3 axis = vol.starRegionNormal[index].xyz, u = vol.starRegionTangent[index].xyz, v = cross(axis, u);
        float a = vol.starRegionNormal[index].w, h = vol.starRegionTangent[index].w;
        for (int strand = 0; strand < 7; ++strand) {
            float spread = (strand - 3) * .003;
            float separation = a * (.72 + .07 * strand), arcHeight = h * (.55 + .1 * strand);
            vec3 c = axis * sqrt(1 - separation * separation - spread * spread) + v * spread;
            vec3 delta = p - c;
            float x = dot(delta, u), y = dot(delta, v), z = dot(delta, axis);
            if (z < -.006)
                continue;
            for (int layer = 0; layer < 2; ++layer) {
                // Cool material occupies selected lower strands, not a second opaque shell.
                if (layer == 1 && strand != 2 && strand != 5)
                    continue;
                float top = layer == 0 ? arcHeight : arcHeight * .68;
                float ellipse = length(vec2(x / separation, max(0, z) / top));
                float distance = (ellipse - 1) * min(separation, top);
                vec3 radial =
                    normalize(u * (x / (separation * separation)) + axis * (max(z, .00001) / (top * top)));
                float theta = atan(max(0, z) / top, x / separation);
                float bend = .0015 * sin(theta) * sin(theta * 4 + j + flow * .3);
                vec2 tube = activityTube(y - bend, distance, layer == 0 ? .0013 : .0022, dot(rotatedRay, v),
                                         dot(rotatedRay, radial), footprint, ds);
                if (tube.x < 1e-7 && tube.y < 1e-7)
                    continue;
                float pulse =
                    .3 +
                    .7 * pow(.5 + .5 * sin(theta * (13 + strand) - flow / max(h, .01) + j * 2.1 + strand), 2);
                float peak = (layer == 0 ? 15. : 9.) * strength * pulse;
                field.electrons += peak * tube.x;
                if (layer == 0)
                    field.hotMeasure += peak * peak * tube.y;
                else
                    field.coolMeasure += peak * peak * tube.y;
            }
        }
    }
    if (height < .023 && !hasPlasma(star)) {
        vec2 cells = activityCells(n * 70, vol.starMotion[star].x * .4, uint(vol.starMotion[star].w) + 901u);
        float core = pow(max(0, 1 - cells.x * 2.4), 5);
        float top = .004 + .01 * (.5 + .5 * sin(n.x * 311 + n.z * 257 + vol.starMotion[star].x * .6));
        float spine = core * (1 - smoothstep(top, top + .002, height)) * exp(-height / .012);
        float chromo = exp(-height / .0025);
        float density = 18 * spine + 3 * chromo;
        field.electrons += density;
        field.coolMeasure += density * density;
    }
    field.electrons *= taper;
    field.hotMeasure *= taper;
    field.coolMeasure *= taper;
    if (hasPlasma(star) && includePatch)
        plasmaField(p, field);
    return field;
}
bool activityInterval(vec3 ro, vec3 rd, float radius, out float nearT, out float farT) {
    float centre = -dot(ro, rd);
    vec3 perpendicular = cross(ro, rd);
    float d = radius * radius - dot(perpendicular, perpendicular);
    if (d <= 0) {
        nearT = farT = 0;
        return false;
    }
    float halfChord = sqrt(d);
    nearT = max(0, centre - halfChord);
    farT = centre + halfChord;
    return farT > nearT;
}
float activityOcclusion(vec3 point, vec3 ray, int emitter) {
    float first = 1e30;
    int stars = int(vol.stellarCounts.x), bodies = int(vol.stellarCounts.y);
    for (int k = 0; k < stars + bodies; ++k) {
        vec4 sphere;
        if (k < stars) {
            if (k == emitter)
                continue;
            sphere = vol.starPositionRadius[k];
        } else
            sphere = vol.occultingSpheres[k - stars];
        float hit = sphereEntry(point, ray, sphere);
        if (hit >= 0)
            first = min(first, hit);
    }
    return first;
}
void activityIntegrateRange(inout vec3 light, inout float transmission, vec3 ro, vec3 ray, float lo, float hi,
                            int count, float footprint, int star, int mode) {
    if (hi <= lo || count <= 0)
        return;
    float ds = (hi - lo) / count;
    float tau = vol.starAtmosphere[star].y;
    vec3 base = vol.starRadiance[star].rgb;
    float meanBand = dot(base, vec3(.3333333));
    for (int k = 0; k < 192; ++k) {
        if (k >= count || transmission < .001)
            break;
        vec3 p = ro + ray * (lo + (k + .5) * ds);
        ActivityField f = activityAtmosphere(p, ray, footprint, ds, star, true);
        float radius = max(1, length(p));
        float sigma;
        vec3 source;
        if (mode == 1 || mode == 3) {
            sigma = sqrt(max(0, f.coolMeasure)) * 35;
            source = meanBand * (mode == 3 ? vec3(1) : vec3(1, .36, .06)) * f.hotMeasure * .15;
        } else {
            sigma = tau * f.electrons;
            float q = 1 / radius;
            float omega = 6.283185307 * q * q / (1 + sqrt(max(0, 1 - q * q)));
            float cosine = dot(normalize(p), -ray);
            float phase = 3 * (1 + cosine * cosine) / (16 * STELLAR_PI);
            source = base * (tau * f.electrons * omega * phase);
            source += vec3(base.r, base.g * .025, 0) * f.coolMeasure * 2e-6;
        }
        float optical = sigma * ds, tr = exp(-optical),
              integral = optical < .001 ? ds * (1 - optical * .5 + optical * optical / 6) : (1 - tr) / sigma;
        light += transmission * source * integral;
        transmission *= tr;
    }
}
// Integrate a consistent quiet baseline, then replace the local transfer.
// An unperturbed patch cancels exactly, regardless of its projected bounds.
vec4 activityMhdComposite(vec3 ro, vec3 ray, float lo, float hi, float nearPatch, float farPatch, bool hit,
                          float footprint, int star, int mode) {
    int budget = int(vol.starAtmosphere[star].x), baseCount = max(8, budget / 4),
        patchCount = budget - baseCount;
    nearPatch = clamp(nearPatch, lo, hi);
    farPatch = clamp(farPatch, lo, hi);
    vec3 gain =
        dot(vol.starRadiance[star].rgb, vec3(.3333333)) * .15 * (mode == 3 ? vec3(1) : vec3(1, .36, .06));
    vec3 baseline = vec3(0), behind = vec3(0);
    float ds = (hi - lo) / baseCount;
    for (int i = 0; i < 192; ++i) {
        if (i >= baseCount)
            break;
        float a = lo + i * ds, b = a + ds;
        vec3 p = ro + ray * (a + .5 * ds);
        ActivityField f = activityAtmosphere(p, ray, footprint, ds, star, false);
        vec3 source = gain * f.hotMeasure;
        baseline += source * ds;
        behind += source * max(0, b - max(a, farPatch));
    }
    if (!hit || farPatch <= nearPatch)
        return vec4(baseline, 1);
    vec3 actual = vec3(0), replaced = vec3(0);
    float transmission = 1;
    ds = (farPatch - nearPatch) / patchCount;
    for (int i = 0; i < 192; ++i) {
        if (i >= patchCount)
            break;
        vec3 p = ro + ray * (nearPatch + (i + .5) * ds);
        ActivityField quiet = activityAtmosphere(p, ray, footprint, ds, star, false), f = quiet;
        plasmaField(activityFrame(p, star), f);
        float sigma = sqrt(max(0, f.coolMeasure)) * 35, optical = sigma * ds, tr = exp(-optical);
        float integral = optical < .001 ? ds * (1 - optical * .5 + optical * optical / 6) : (1 - tr) / sigma;
        actual += transmission * gain * f.hotMeasure * integral;
        replaced += gain * quiet.hotMeasure * ds;
        transmission *= tr;
    }
    return vec4(max(vec3(0), baseline + actual - replaced + (transmission - 1) * behind), transmission);
}
vec4 activityTransport(vec3 ro, vec3 ray, float coreHit, float clipping, float footprint, int star,
                       int mode) {
    float lo, hi;
    vec3 light = vec3(0);
    float transmission = 1;
    if (vol.starAtmosphere[star].y <= 0 || !activityInterval(ro, ray, vol.starAtmosphere[star].z, lo, hi))
        return vec4(light, transmission);
    hi = min(hi, clipping);
    if (coreHit >= 0)
        hi = min(hi, coreHit);
    if (hi <= lo)
        return vec4(light, transmission);
    int budget = int(vol.starAtmosphere[star].x);
    float thinLo = lo, thinHi = lo;
    bool patchHit = false;
    if (hasPlasma(star)) {
        float scale = vol.plasmaTangent.w;
        float halfX = max(abs(vol.plasmaGrid.z), abs(vol.plasmaGrid.z + vol.plasmaExtent.x));
        float halfY = vol.plasmaExtent.y * .5;
        vec3 centre = activityRotate(vol.plasmaNormal.xyz * (1 + (vol.plasmaGrid.w + halfY) * scale),
                                     -activityAngle(vol.plasmaNormal.xyz, star));
        float bound = scale * length(vec3(halfX, halfY, vol.plasmaExtent.z * 3.5));
        patchHit = activityInterval(ro - centre, ray, bound, thinLo, thinHi) && thinHi > lo && thinLo < hi;
    }
    if (hasPlasma(star) && (mode == 1 || mode == 3))
        return activityMhdComposite(ro, ray, lo, hi, thinLo, thinHi, patchHit, footprint, star, mode);
    if (patchHit || (activityInterval(ro, ray, 1.023, thinLo, thinHi) && thinHi > lo && thinLo < hi)) {
        thinLo = clamp(thinLo, lo, hi);
        thinHi = clamp(thinHi, lo, hi);
        int inner = patchHit ? budget * 3 / 4 : min(20, budget / 3), outer = budget - inner;
        float front = thinLo - lo, back = hi - thinHi;
        int frontCount = front + back > 1e-6 ? int(round(outer * front / (front + back))) : 0;
        activityIntegrateRange(light, transmission, ro, ray, lo, thinLo, frontCount, footprint, star, mode);
        activityIntegrateRange(light, transmission, ro, ray, thinLo, thinHi, inner, footprint, star, mode);
        activityIntegrateRange(light, transmission, ro, ray, thinHi, hi, outer - frontCount, footprint, star,
                               mode);
    } else
        activityIntegrateRange(light, transmission, ro, ray, lo, hi, budget, footprint, star, mode);
    return vec4(light, transmission);
}

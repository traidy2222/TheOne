#version 450
#extension GL_GOOGLE_include_directive : require
#include "volume_common.glsl"
#include "stellar.glsl"
#include "stellar_activity.glsl"
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
void main() {
    // Translation-free camera basis avoids cancellation in inverse VP at a star.
    vec3 right=normalize(cross(vol.forward.xyz,vec3(0,1,0)));
    vec3 up=cross(right,vol.forward.xyz);
    float c=cos(vol.plasmaExtent.w),s=sin(vol.plasmaExtent.w);
    vec3 rolledRight=right*c-up*s;up=up*c+right*s;right=rolledRight;
    vec2 ndc=uv*2-1;
    vec3 direction=normalize(vol.forward.xyz+vol.forward.w*(right*ndc.x*vol.eye.w-up*ndc.y)),color=vec3(0);
    float pixelArea = max(1e-14, length(cross(dFdx(direction), dFdy(direction))));
    float width = sqrt(pixelArea);
    for (int star = 0; star < int(vol.stellarCounts.x); ++star) {
        vec3 axis, tangent, bitangent;
        float oneMinusCos, omega;
        stellarCone(vol.eye.xyz, star, axis, tangent, bitangent, oneMinusCos, omega);
        if (dot(axis, direction) <= 0 && vol.starActivity[star].x<.5)
            continue;
        float angularRadius = asin(clamp(sqrt(oneMinusCos * (2 - oneMinusCos)), 0, 1));
        if(dot(axis,direction)<=0 && angularRadius<=width*1.5)continue;
        float separation = asin(clamp(length(cross(axis, direction)), 0, 1)), coverage;
        if(vol.starActivity[star].x>.5 && angularRadius>width*1.5){
            float radius=vol.starPositionRadius[star].w;
            vec3 ro=(vol.eye.xyz-vol.starPositionRadius[star].xyz)/radius;
            float lo,hi;
            if(!activityInterval(ro,direction,vol.starAtmosphere[star].z,lo,hi))continue;
            int mode=int(vol.stellarCounts.w);
            // An instrument mask, not a change to the emitter's luminosity.
            if(mode==2 && separation<angularRadius*1.025)continue;
            float core=sphereEntry(vol.eye.xyz,direction,vol.starPositionRadius[star]);
            float clipping=activityOcclusion(vol.eye.xyz,direction,star)/radius;
            float footprint=width*max(.0001,length(ro)-1);
            vec3 surface=vec3(0);
            if(core>=0 && core/radius<clipping){
                vec3 normal=normalize(ro+direction*(core/radius));
                float mu=max(0,dot(normal,-direction));
                surface=(mode==1||mode==3)?activityEuvSurface(normal,star):activityPhotosphere(normal,mu,footprint,star);
            }
            vec4 atmosphere=activityTransport(ro,direction,core<0?-1:core/radius,clipping,footprint,star,mode);
            color+=atmosphere.rgb+atmosphere.a*surface;
            continue;
        }
        if (angularRadius > width * 1.5)
            coverage = 1 - smoothstep(angularRadius - width * .5, angularRadius + width * .5, separation);
        else {
            float variance = pixelArea * .5;
            coverage =
                min(1, omega / (2 * STELLAR_PI * variance) * exp(-.5 * separation * separation / variance));
        }
        float end = sphereEntry(vol.eye.xyz, axis, vol.starPositionRadius[star]);
        if (stellarBlocked(vol.eye.xyz, direction, max(0, end), -1, star))
            coverage = 0;
        color += vol.starRadiance[star].rgb * coverage;
    }
    outColor = vec4(min(vec3(60000), color * HDR_STORAGE_SCALE), 1);
}

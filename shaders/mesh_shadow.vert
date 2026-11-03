#version 450
#extension GL_GOOGLE_include_directive : require
#include "skinning.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=4) in uvec4 joints;
layout(location=5) in vec4 weights;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
void main() {
    vec3 p,n;transformVertex(gl_InstanceIndex,position,normal,joints,weights,p,n);
    gl_Position=f.vp*vec4(p,1);
}

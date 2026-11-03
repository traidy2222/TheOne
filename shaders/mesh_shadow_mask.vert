#version 450
#extension GL_GOOGLE_include_directive : require
#include "skinning.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(location=4) in uvec4 joints;
layout(location=5) in vec4 weights;
layout(location=0) out float vertexAlpha;
layout(location=1) out vec2 texCoord;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
void main() {
    vec3 p,n;transformVertex(gl_InstanceIndex,position,normal,joints,weights,p,n);
    vertexAlpha=color.a;texCoord=uv;gl_Position=f.vp*vec4(p,1);
}

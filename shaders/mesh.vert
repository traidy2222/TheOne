#version 450
#extension GL_GOOGLE_include_directive : require
#include "skinning.glsl"
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(location=4) in uvec4 joints;
layout(location=5) in vec4 weights;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
#include "scene_uniform.glsl"
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec4 vertexColor;
layout(location=3) out vec2 texCoord;

void main() {
    transformVertex(gl_InstanceIndex,position,normal,joints,weights,worldPosition,worldNormal);
    vertexColor=color;texCoord=uv;
    gl_Position=f.vp*vec4(worldPosition,1);
}

#version 450
layout(location=0) in vec3 position;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(location=0) out float vertexAlpha;
layout(location=1) out vec2 texCoord;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
void main() { vertexAlpha=color.a;texCoord=uv;gl_Position=f.vp*vec4(position,1); }

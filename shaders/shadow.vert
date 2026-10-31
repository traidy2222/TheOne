#version 450
layout(location=0) in vec3 position;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
void main() { gl_Position=f.vp*vec4(position,1); }

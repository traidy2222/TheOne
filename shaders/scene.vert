#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
layout(set=0,binding=1) uniform Scene { mat4 lightVP; vec4 lightDirection; vec4 options; } scene;
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec4 vertexColor;
layout(location=3) out vec2 texCoord;
layout(location=4) out vec4 shadowCoord;
void main() { worldPosition=position; worldNormal=normal; vertexColor=color; texCoord=uv; shadowCoord=scene.lightVP*vec4(position,1); gl_Position=f.vp*vec4(position,1); }

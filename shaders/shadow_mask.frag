#version 450
layout(set=0,binding=0) uniform sampler2D baseTexture;
layout(location=0) in float vertexAlpha;
layout(location=1) in vec2 texCoord;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
void main() {
    if(texture(baseTexture,texCoord).a * vertexAlpha * f.baseColor.a < f.params.z) discard;
}

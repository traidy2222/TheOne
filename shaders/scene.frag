#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec4 vertexColor;
layout(location=3) in vec2 texCoord;
layout(location=4) in vec4 shadowCoord;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0) uniform sampler2D baseTexture;
layout(set=0,binding=1) uniform Scene { mat4 lightVP; vec4 lightDirection; vec4 options; } scene;
layout(set=0,binding=2) uniform sampler2DShadow shadowMap;
layout(set=0,binding=3) uniform sampler2D metallicRoughnessTexture;
layout(set=0,binding=4) uniform sampler2D normalTexture;
layout(set=0,binding=5) uniform sampler2D occlusionTexture;
layout(set=0,binding=6) uniform sampler2D emissiveTexture;
layout(set=0,binding=7) uniform Material { vec4 emissiveFactor; } material;
layout(push_constant) uniform Frame { mat4 vp; vec4 eyeTime; vec4 baseColor; vec4 params; vec4 lighting; } f;
const float PI=3.14159265;
void main() {
    vec4 base=texture(baseTexture,texCoord)*vertexColor*f.baseColor;
    if(f.params.z>=0 && base.a<f.params.z) discard;
    vec3 n=normalize(worldNormal); if(!gl_FrontFacing)n=-n;
    vec3 dp1=dFdx(worldPosition),dp2=dFdy(worldPosition);vec2 duv1=dFdx(texCoord),duv2=dFdy(texCoord);
    float uvDet=duv1.x*duv2.y-duv1.y*duv2.x;
    if(abs(uvDet)>0.000001) {
        vec3 tangent=normalize((dp1*duv2.y-dp2*duv1.y)/uvDet);tangent=normalize(tangent-n*dot(n,tangent));
        vec3 bitangent=normalize(cross(n,tangent))*sign(uvDet);
        vec3 mapped=texture(normalTexture,texCoord).xyz*2-1;mapped.xy*=f.lighting.z;
        n=normalize(mat3(tangent,bitangent,n)*mapped);
    }
    vec3 v=normalize(f.eyeTime.xyz-worldPosition), l=normalize(scene.lightDirection.xyz), h=normalize(v+l);
    float nl=max(dot(n,l),0),nv=max(dot(n,v),0.001),nh=max(dot(n,h),0),vh=max(dot(v,h),0);
    vec4 mr=texture(metallicRoughnessTexture,texCoord);
    float rough=clamp(f.params.x*mr.g,0.08,1),metal=clamp(f.params.y*mr.b,0,1),a=rough*rough,a2=a*a;
    float denominator=nh*nh*(a2-1)+1; float D=a2/(PI*denominator*denominator+0.0001);
    float k=(rough+1)*(rough+1)/8; float G=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
    vec3 F0=mix(vec3(0.04),base.rgb,metal),F=F0+(1-F0)*pow(1-vh,5);
    vec3 specular=D*G*F/(4*nv*max(nl,0.001)+0.0001);
    vec3 p=shadowCoord.xyz/shadowCoord.w; vec2 uv=p.xy*0.5+0.5; float visibility=1;
    if(scene.options.x>0 && p.z>0 && p.z<1 && all(greaterThanEqual(uv,vec2(0))) && all(lessThanEqual(uv,vec2(1)))) {
        visibility=0; float bias=max(0.0006*(1-nl),0.00015);
        for(int x=-1;x<=1;++x) for(int y=-1;y<=1;++y) visibility+=texture(shadowMap,vec3(uv+vec2(x,y)/2048.0,p.z-bias))/9;
    }
    float ao=mix(1,texture(occlusionTexture,texCoord).r,f.lighting.w);
    vec3 color=base.rgb*0.15*ao+((1-F)*(1-metal)*base.rgb/PI+specular)*nl*3.0*visibility;
    color+=texture(emissiveTexture,texCoord).rgb*material.emissiveFactor.rgb;
    if(f.lighting.y>0.5)color=base.rgb;
    color=mix(color,vec3(1,0.55,0.07),f.params.w*0.04);
    float fog=1-exp(-length(worldPosition-f.eyeTime.xyz)*0.012); color=mix(color,vec3(0.035,0.055,0.09),fog);
    color=color/(color+vec3(1)); if(f.lighting.x<0.5)color=pow(max(color,vec3(0)),vec3(1.0/2.2)); outColor=vec4(color,base.a);
}

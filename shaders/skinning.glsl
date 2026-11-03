struct GeometryTransform { mat4 position; mat4 normal; uvec4 info; };
layout(std430,set=1,binding=0) readonly buffer Geometry { GeometryTransform transforms[]; };
void transformVertex(uint base,vec3 position,vec3 normal,uvec4 joints,vec4 weights,out vec3 p,out vec3 n) {
    float total=dot(weights,vec4(1));
    if(transforms[base].info.x>0 && total>0.00001) {
        vec4 result=vec4(0);n=vec3(0);
        for(int i=0;i<4;++i)if(weights[i]>0) {
            uint index=base+1+joints[i];float weight=weights[i]/total;
            result+=transforms[index].position*vec4(position,1)*weight;
            n+=mat3(transforms[index].normal)*normal*weight;
        }
        p=result.xyz;
    }else {
        p=(transforms[base].position*vec4(position,1)).xyz;
        n=mat3(transforms[base].normal)*normal;
    }
    n=length(n)>1e-8?normalize(n):vec3(0,1,0);
}

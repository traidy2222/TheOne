layout(set=0,binding=2) uniform sampler2DArrayShadow shadowMap;
float cascadeVisibility(int cascade,vec3 world,float nl) {
    vec4 clip=scene.lightVP[cascade]*vec4(world,1);vec3 p=clip.xyz/clip.w;vec2 uv=p.xy*.5+.5;
    if(p.z<=0 || p.z>=1 || any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))return 1;
    float visibility=0;float bias=max(.00035*(1-nl),.00008);
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)
        visibility+=texture(shadowMap,vec4(uv+vec2(x,y)/scene.options.z,float(cascade),p.z-bias))/9;
    return visibility;
}
float shadowVisibility(vec3 world,vec3 eye,float nl) {
    if(scene.options.x<=0)return 1;
    float depth=dot(world-eye,scene.cameraForward.xyz);if(depth>scene.splits.z)return 1;
    int cascade=depth>scene.splits.y?2:(depth>scene.splits.x?1:0);
    float visibility=cascadeVisibility(cascade,world,nl);
    float start=cascade==0?scene.splits.w:scene.splits[cascade-1],end=scene.splits[cascade];
    float blend=smoothstep(mix(start,end,.9),end,depth);
    if(blend>0)visibility=mix(visibility,cascade<2?cascadeVisibility(cascade+1,world,nl):1,blend);
    return visibility;
}

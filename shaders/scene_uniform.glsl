layout(set=0,binding=1) uniform Scene {
    mat4 lightVP[3]; vec4 lightDirection; vec4 options; vec4 splits; vec4 cameraForward;
} scene;

#include <theone/render/renderer.hpp>
#include <theone/scene/world.hpp>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <theone/effects/particles.hpp>
#include <imgui_impl_sdl3.h>
#include <numeric>

static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
static std::vector<unsigned char> capture() {
    std::ifstream file("capture.ppm",std::ios::binary);std::string magic;int w,h,maximum;
    file>>magic>>w>>h>>maximum;file.get();require(magic=="P6" && maximum==255,"Invalid capture");
    std::vector<unsigned char> bytes(size_t(w)*h*3);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    require(bool(file),"Incomplete capture");return bytes;
}
static void compare(const std::vector<unsigned char>& cpu,const std::vector<unsigned char>& gpu) {
    require(cpu.size()==gpu.size(),"Capture dimensions changed");double error=0;size_t different=0;
    for(size_t i=0;i<cpu.size();++i) {int d=std::abs(int(cpu[i])-int(gpu[i]));error+=d;if(d>8)++different;}
    std::cout<<"CPU/GPU pixels: mean error "<<error/cpu.size()<<", differing channels "<<different<<" / "<<cpu.size()<<'\n';
    require(error/cpu.size()<.5 && double(different)/cpu.size()<.005,"GPU geometry differs from CPU reference");
}
int main(int argc,char** argv) {
    try {
        require(argc>=2,"Expected fixture directory");bool validation=argc>2 && std::string(argv[2])=="--validate";
        auto world=one::World::load(std::filesystem::path(argv[1])/"sandbox.one");
        require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());struct Quit{~Quit(){SDL_Quit();}} quit;
        auto* window=SDL_CreateWindow("TheOne GPU geometry regression",640,480,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE);
        require(window!=nullptr,SDL_GetError());struct Window{SDL_Window* value;~Window(){SDL_DestroyWindow(value);}} guard{window};
        ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;require(ImGui_ImplSDL3_InitForVulkan(window),"UI initialization failed");
        struct UI{~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}} ui;
        one::Renderer renderer;renderer.initialize(window,validation);one::Camera camera;
        auto frame=[&] {SDL_PumpEvents();require(renderer.beginFrame(),"Frame unavailable");ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();ImGui::Render();};
        std::vector<one::Vertex> vertices,dynamic;std::vector<one::Draw> draws,dynamicDraws;one::RenderScene scene;
        one::ParticleSystem particles;for(int i=0;i<80;++i)particles.step(1.f/60);
        particles.bake(camera,dynamic,dynamicDraws);
        std::vector<unsigned char> initialPose;
        for(int test=0;test<5;++test) {
            auto* robot=world.find(2);robot->transform.scale=test>=2?glm::vec3(1.4f,.7f,1.1f):glm::vec3(1);
            robot->transform.rotation=glm::angleAxis(.2f*test,glm::normalize(glm::vec3(1,2,0)));
            robot->animationLoop=test!=3;robot->previousClip=test==4?0:-1;robot->previousAnimationTime=.1f;robot->animationBlend=.35f;
            float time=test==3?20.f:.17f*test;
            world.bake(time,vertices,draws);particles.bake(camera,vertices,draws);
            frame();require(renderer.render(vertices,draws,camera,time,true,true,2),"CPU render failed");auto cpu=capture();
            if(test==0)initialPose=cpu;
            if(test==1) {
                size_t changed=0;for(size_t i=0;i<cpu.size();++i)if(cpu[i]!=initialPose[i])++changed;
                require(changed>50,"Animated fixture is not visibly changing; parity test would be vacuous");
            }
            world.prepare(time,scene);frame();require(renderer.render(scene,dynamic,dynamicDraws,camera,time,true,true,2),"GPU render failed");
            compare(cpu,capture());
            if(test)require(renderer.geometryStats().uploadedGeometryBytes==0,"Unchanged GPU mesh reuploaded");
            require(renderer.geometryStats().uploadedPaletteBytes==scene.transforms.size()*sizeof(one::GeometryTransform),"Palette upload accounting failed");
        }
        // CPU fallback and persistent procedural geometry can alternate with GPU meshes.
        world.bake(.25f,vertices,draws);
        frame();renderer.renderCached(vertices,draws,1,camera,0,true);auto cached=capture();
        require(renderer.geometryStats().uploadedDynamicBytes>0,"Initial procedural upload missing");
        frame();renderer.renderCached(vertices,draws,1,camera,0,true);compare(cached,capture());
        // Both frame slots hold independent procedural buffers.
        frame();renderer.renderCached(vertices,draws,1,camera,0,true);compare(cached,capture());
        require(renderer.geometryStats().uploadedDynamicBytes==0,"Unchanged procedural geometry reuploaded");
        vertices[0].position.y+=.01f;frame();renderer.renderCached(vertices,draws,2,camera,0);
        require(renderer.geometryStats().uploadedDynamicBytes>0,"Procedural revision was ignored");
        renderer.clearAssets();world.prepare(.5f,scene);frame();renderer.render(scene,dynamic,dynamicDraws,camera,0,true);
        require(renderer.geometryStats().uploadedGeometryBytes>0,"Asset reset did not rebuild meshes");
        SDL_SetWindowSize(window,800,600);renderer.resize();frame();renderer.render(scene,dynamic,dynamicDraws,camera,0,true);
        require(renderer.geometryStats().uploadedGeometryBytes==0,"Resize discarded resident meshes");
        one::RenderScene empty;frame();renderer.render(empty,{}, {},camera,0);
        // Same animated mesh shared by many entities: CPU cost must scale with poses, not vertices.
        const auto cachedBeforeCrowd=renderer.geometryStats().cachedMeshes;
        one::World crowd;auto robot=*world.find(2);robot.parent=0;
        for(uint64_t i=1;i<=256;++i){robot.id=i;robot.transform.position={float(i%16)*2,0,float(i/16)*2};crowd.entities.push_back(robot);}
        crowd.bake(.3f,vertices,draws);crowd.prepare(.3f,scene);
        constexpr int samples=80;
        auto measure=[&](auto operation) {auto start=std::chrono::steady_clock::now();for(int i=0;i<samples;++i)operation(float(i)*.013f);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/samples;};
        double cpuMs=measure([&](float t){crowd.bake(t,vertices,draws);});
        double gpuPrepMs=measure([&](float t){crowd.prepare(t,scene);});
        std::cout<<"BENCHMARK 256 animated instances, "<<vertices.size()<<" vertices: CPU bake "<<cpuMs<<" ms, GPU pose preparation "<<gpuPrepMs<<" ms, ratio "<<cpuMs/gpuPrepMs<<"x\n";
        frame();renderer.render(scene,{}, {},camera,0);
        frame();renderer.render(scene,{}, {},camera,0);
        auto stats=renderer.geometryStats();require(stats.uploadedGeometryBytes==0,"Shared geometry reuploaded");
        require(stats.cachedMeshes==cachedBeforeCrowd,"Identical instances did not share GPU buffers");
        std::cout<<"STEADY UPLOAD: old vertices "<<vertices.size()*sizeof(one::Vertex)<<" bytes, palettes "<<stats.uploadedPaletteBytes<<" bytes, geometry "<<stats.uploadedGeometryBytes<<" bytes; resident "<<stats.residentGeometryBytes<<" bytes\n";
        renderer.close();require(renderer.errors()==0,"Vulkan validation failed");
        std::cout<<"PASS: GPU skinning, shadows, crossfade, nonuniform scale, procedural cache, invalidation, resize and shared mesh residency\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}

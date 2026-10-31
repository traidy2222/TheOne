#include "application.hpp"
#include "renderer.hpp"
#include "game.hpp"
#include "effects.hpp"
#include "input.hpp"
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>
#include <deque>
#include <limits>

namespace {
struct Session {
    one::World world;
    std::unique_ptr<one::Physics> physics=std::make_unique<one::Physics>();
    std::unique_ptr<one::GameModule> game=one::createSandboxGame();
    one::Camera camera;
    one::ParticleSystem effects;
    std::vector<one::World> undo,redo;
    std::vector<std::string> log;
    std::vector<one::Vertex> vertices; std::vector<one::Draw> draws;
    bool simulate=true,walk=false,shadows=true,showEditor=true,animation=true,dirty=false;
    uint64_t selected=2;
    float time=0,accumulator=0;
    glm::vec3 desiredMovement{0}; bool jump=false;
    SDL_AudioStream* audio=nullptr;
    std::vector<uint8_t> sound;
    std::filesystem::path savePath="saves/scene.one";
    std::string status="Ready";
    ~Session() { if(audio) SDL_DestroyAudioStream(audio); }
    void message(std::string text) { status=text; log.push_back(text); std::cout<<text<<std::endl; }
    void soundEffect() { if(audio) { if(!SDL_PutAudioStreamData(audio,sound.data(),static_cast<int>(sound.size()))) message(SDL_GetError()); } }
    one::GameServices services() { return {world,*physics,[this] { soundEffect(); }}; }
    void checkpoint() { undo.push_back(world); if(undo.size()>32) undo.erase(undo.begin()); redo.clear(); dirty=true; }
    void rebuild() { physics->rebuild(world); auto s=services(); game->start(s); accumulator=0; }
    void load(std::filesystem::path path,Renderer& renderer) {
        auto next=one::World::load(path);
        // Build physics before replacing the live scene so failed imports preserve the session.
        auto nextPhysics=std::make_unique<one::Physics>(); nextPhysics->rebuild(next);
        renderer.clearAssets(); world=std::move(next); physics=std::move(nextPhysics); undo.clear();redo.clear();time=0;dirty=false;selected=world.entities.empty()?0:world.entities.front().id;
        auto s=services();game->start(s);message("Loaded "+path.string());
    }
    void import(const std::filesystem::path& path) {
        auto model=one::Model::load(path); checkpoint();
        uint64_t id=1; for(const auto& e:world.entities) id=std::max(id,e.id+1);
        one::Entity e;e.id=id;e.name=path.stem().string();e.asset=std::filesystem::absolute(path).generic_string();e.model=std::move(model);
        e.transform.position=camera.position+camera.forward()*5.0f;world.entities.push_back(std::move(e));selected=id;
        message("Imported "+path.string());
    }
    void history(bool back,Renderer& renderer) {
        auto& from=back?undo:redo;auto& to=back?redo:undo;
        if(from.empty())return;to.push_back(world);renderer.clearAssets();world=std::move(from.back());from.pop_back();rebuild();dirty=true;
    }
    void save() { std::filesystem::create_directories(savePath.parent_path()); world.save(savePath);dirty=false;message("Saved "+savePath.string()); }
    void saveGame() {
        std::filesystem::create_directories("saves");world.save("saves/game.one");auto p=physics->playerPosition();
        nlohmann::json j={{"player",{p.x,p.y,p.z}},{"time",time},{"yaw",camera.yaw},{"pitch",camera.pitch},{"walk",walk},{"camera",{camera.position.x,camera.position.y,camera.position.z}}};
        j["version"]=1;auto velocity=physics->playerVelocity();j["playerVelocity"]={velocity.x,velocity.y,velocity.z};j["bodies"]=nlohmann::json::array();
        for(const auto& motion:physics->motions())j["bodies"].push_back({{"id",motion.id},{"linear",{motion.linear.x,motion.linear.y,motion.linear.z}},{"angular",{motion.angular.x,motion.angular.y,motion.angular.z}}});
        std::ofstream out("saves/game.json");out<<j.dump(2);if(!out)throw std::runtime_error("Cannot write gameplay save");message("Gameplay saved");
    }
    void loadGame(Renderer& renderer) {
        std::ifstream input("saves/game.json");nlohmann::json j;input>>j;
        if(j.at("version")!=1)throw std::runtime_error("Unsupported gameplay save version");
        auto vector=[](const nlohmann::json& a){
            if(!a.is_array() || a.size()!=3)throw std::runtime_error("Invalid saved vector");
            glm::vec3 v(a[0].get<float>(),a[1].get<float>(),a[2].get<float>());
            for(int i=0;i<3;++i)if(!std::isfinite(v[i]))throw std::runtime_error("Non-finite saved vector");return v;
        };
        auto position=vector(j.at("player")),velocity=vector(j.at("playerVelocity")),cameraPosition=vector(j.at("camera"));
        float savedTime=j.at("time").get<float>(),yaw=j.at("yaw").get<float>(),pitch=j.at("pitch").get<float>();bool savedWalk=j.at("walk").get<bool>();
        if(!std::isfinite(savedTime) || savedTime<0 || !std::isfinite(yaw) || !std::isfinite(pitch))throw std::runtime_error("Invalid saved clock/camera");
        std::vector<one::BodyMotion> motions;
        if(j.at("bodies").size()>8192)throw std::runtime_error("Saved body limit exceeded");
        for(const auto& body:j.at("bodies"))motions.push_back({body.at("id").get<uint64_t>(),vector(body.at("linear")),vector(body.at("angular"))});
        load("saves/game.one",renderer);physics->resetPlayer(position);physics->setPlayerVelocity(velocity);physics->restoreMotions(motions);
        time=savedTime;camera.position=cameraPosition;camera.yaw=yaw;camera.pitch=std::clamp(pitch,-1.5f,1.5f);walk=savedWalk;message("Gameplay loaded");
    }
};
void theme() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=7;s.FrameRounding=4;s.GrabRounding=4;s.WindowPadding={14,12};
    s.Colors[ImGuiCol_WindowBg]={.045f,.06f,.085f,.96f};s.Colors[ImGuiCol_Header]={.12f,.28f,.34f,1};s.Colors[ImGuiCol_Button]={.10f,.29f,.34f,1};s.Colors[ImGuiCol_CheckMark]={.2f,.85f,.75f,1};
}
void editor(Session& s,Renderer& renderer) {
    auto action=[&](const std::function<void()>& f){try{f();}catch(const std::exception& e){s.message(std::string("Error: ")+e.what());}};
    if(ImGui::BeginMainMenuBar()) {
        ImGui::TextColored({.25f,.9f,.8f,1},"THE ONE");ImGui::SameLine();ImGui::TextDisabled("IMPORT SANDBOX");ImGui::SameLine();
        if(ImGui::Button(s.simulate?"Pause":"Simulate"))s.simulate=!s.simulate;ImGui::SameLine();
        if(ImGui::Button(s.walk?"Fly camera [Tab]":"Walk / play [Tab]")){s.walk=!s.walk;if(s.walk)s.simulate=true;}ImGui::SameLine();
        if(ImGui::Button("Save scene"))action([&]{s.save();});ImGui::SameLine();
        if(ImGui::Button("Load saved"))action([&]{s.load(s.savePath,renderer);});ImGui::SameLine();
        if(ImGui::Button("Undo"))action([&]{s.history(true,renderer);});ImGui::SameLine();
        if(ImGui::Button("Redo"))action([&]{s.history(false,renderer);});ImGui::SameLine();
        ImGui::TextDisabled("%.1f fps | %.2f ms",ImGui::GetIO().Framerate,1000.0f/std::max(ImGui::GetIO().Framerate,1.0f));
        ImGui::EndMainMenuBar();
    }
    const auto size=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({12,44},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({280,430},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Scene hierarchy")) {
        ImGui::TextDisabled("%zu entities | %zu triangles",s.world.entities.size(),s.vertices.size()/3);ImGui::Separator();
        std::function<void(uint64_t)> tree=[&](uint64_t parent) {
            for(auto& e:s.world.entities)if(e.parent==parent) {
                bool children=false;for(const auto& other:s.world.entities)if(other.parent==e.id)children=true;
                auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|(children?0:ImGuiTreeNodeFlags_Leaf)|(s.selected==e.id?ImGuiTreeNodeFlags_Selected:0);
                bool open=ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(e.id)),flags,"%s",e.name.c_str());
                if(ImGui::IsItemClicked())s.selected=e.id;
                if(open){tree(e.id);ImGui::TreePop();}
            }
        };tree(0);
        ImGui::Separator();ImGui::TextWrapped("Click an entity or click geometry in the viewport to select it.");
    }ImGui::End();
    ImGui::SetNextWindowPos({size.x-320,44},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({308,500},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Inspector")) {
        auto* e=s.world.find(s.selected);
        if(e) {
            ImGui::TextColored({.25f,.9f,.8f,1},"%s",e->name.c_str());ImGui::TextDisabled("Stable ID: %llu",static_cast<unsigned long long>(e->id));
            bool beforeVisible=e->visible;if(ImGui::Checkbox("Visible",&beforeVisible)){s.checkpoint();e->visible=beforeVisible;}
            // One undo snapshot for a complete drag, rather than one snapshot per pixel.
            auto before=s.world;bool changed=false;
            changed|=ImGui::DragFloat3("Position",glm::value_ptr(e->transform.position),.05f);
            if(ImGui::IsItemActivated()){s.undo.push_back(before);s.redo.clear();}if(ImGui::IsItemDeactivatedAfterEdit())s.dirty=true;
            auto angles=glm::degrees(glm::eulerAngles(e->transform.rotation));before=s.world;
            if(ImGui::DragFloat3("Rotation",glm::value_ptr(angles),.5f)){e->transform.rotation=glm::quat(glm::radians(angles));changed=true;}
            if(ImGui::IsItemActivated()){s.undo.push_back(before);s.redo.clear();}if(ImGui::IsItemDeactivatedAfterEdit())s.dirty=true;
            before=s.world;changed|=ImGui::DragFloat3("Scale",glm::value_ptr(e->transform.scale),.01f,.01f,100, "%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::IsItemActivated()){s.undo.push_back(before);s.redo.clear();}if(ImGui::IsItemDeactivatedAfterEdit())s.dirty=true;
            if(changed){s.simulate=false;action([&]{s.rebuild();});}
            std::string parentName=e->parent && s.world.find(e->parent)?s.world.find(e->parent)->name:"Scene root";
            if(ImGui::BeginCombo("Parent",parentName.c_str())) {
                auto parent=[&](uint64_t id,const char* label){if(ImGui::Selectable(label,e->parent==id)){
                    auto previous=e->parent;auto old=s.world;e->parent=id;
                    try{if(id && e->body!="none")throw std::runtime_error("Physics entities must be roots");s.world.matrix(e->id);s.undo.push_back(old);s.redo.clear();s.dirty=true;}
                    catch(const std::exception& error){e->parent=previous;s.message(error.what());}
                }};
                parent(0,"Scene root");for(const auto& other:s.world.entities)if(other.id!=e->id)parent(other.id,other.name.c_str());ImGui::EndCombo();
            }
            ImGui::TextWrapped("Asset: %s",e->asset.empty()?"Transform only":e->asset.c_str());
            if(e->model) {
                bool anim=e->animate;if(ImGui::Checkbox("Animate",&anim)){s.checkpoint();e->animate=anim;}
                if(!e->model->animations.empty() && ImGui::BeginCombo("Clip",e->model->animations[std::clamp(e->clip,0,static_cast<int>(e->model->animations.size())-1)].name.c_str())) {
                    for(int i=0;i<static_cast<int>(e->model->animations.size());++i)if(ImGui::Selectable(e->model->animations[i].name.c_str(),e->clip==i)){s.checkpoint();e->clip=i;}ImGui::EndCombo();
                }
                if(ImGui::TreeNode("Imported nodes / rig")){for(const auto& n:e->model->nodes)ImGui::BulletText("%s%s",n.name.c_str(),n.skin>=0?" [skin]":"");ImGui::TreePop();}
                if(ImGui::TreeNode("Import diagnostics")){for(const auto& d:e->model->diagnostics)ImGui::TextWrapped("%s",d.c_str());ImGui::TreePop();}
            }
            ImGui::Separator();
            if(ImGui::BeginCombo("Physics",e->body.c_str())){for(const char* body:{"none","static","dynamic"})if(ImGui::Selectable(body,e->body==body))action([&]{if(e->parent && std::string(body)!="none")throw std::runtime_error("Physics bodies must be scene roots");s.checkpoint();e->body=body;s.simulate=false;s.rebuild();});ImGui::EndCombo();}
            if(e->body=="dynamic" && ImGui::Button("Apply upward impulse"))s.physics->impulse(e->id,{0,12,0});
            if(ImGui::Button("Duplicate"))action([&]{auto copy=*e;s.checkpoint();copy.id=1;for(const auto& other:s.world.entities)copy.id=std::max(copy.id,other.id+1);copy.name+=" copy";copy.transform.position.x+=1.5f;s.selected=copy.id;s.world.entities.push_back(copy);s.rebuild();});
        }else ImGui::TextDisabled("Select an entity");
    }ImGui::End();
    ImGui::SetNextWindowPos({12,size.y-210},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({620,198},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Assets and diagnostics")) {
        static char importPath[1024]="";ImGui::InputTextWithHint("##asset","Path to .glb or .gltf",importPath,sizeof(importPath));ImGui::SameLine();if(ImGui::Button("Import"))action([&]{s.import(importPath);});
        ImGui::TextDisabled("Or drop a glTF/GLB file onto the window.");
        ImGui::Checkbox("Shadows",&s.shadows);ImGui::SameLine();ImGui::Checkbox("Animations",&s.animation);ImGui::SameLine();if(ImGui::Button("Play sound"))s.soundEffect();
        ImGui::Separator();ImGui::TextWrapped("%s",s.status.c_str());
        if(ImGui::BeginChild("log",{0,65})){for(const auto& text:s.log)ImGui::TextWrapped("%s",text.c_str());}ImGui::EndChild();
    }ImGui::End();
}
void pick(Session& s,float x,float y,float width,float height) {
    auto inverse=glm::inverse(s.camera.projection(width/height)*s.camera.view());
    glm::vec4 far=inverse*glm::vec4(x/width*2-1,y/height*2-1,1,1);glm::vec3 direction=glm::normalize(glm::vec3(far)/far.w-s.camera.position);
    float nearest=std::numeric_limits<float>::max();uint64_t selected=0;
    for(const auto& draw:s.draws)for(uint32_t i=0;i<draw.count;i+=3) {
        auto a=s.vertices[draw.first+i].position,b=s.vertices[draw.first+i+1].position,c=s.vertices[draw.first+i+2].position;
        auto edge1=b-a,edge2=c-a,h=glm::cross(direction,edge2);float det=glm::dot(edge1,h);if(std::abs(det)<1e-7f)continue;
        float inv=1/det;auto offset=s.camera.position-a;float u=inv*glm::dot(offset,h);if(u<0 || u>1)continue;
        auto q=glm::cross(offset,edge1);float v=inv*glm::dot(direction,q);if(v<0 || u+v>1)continue;
        float t=inv*glm::dot(edge2,q);if(t>0 && t<nearest){nearest=t;selected=draw.entity;}
    }s.selected=selected;
}
}
int runApplication(const Options& options) {
    if(options.smoke)SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD))throw std::runtime_error(SDL_GetError());
    struct Quit { ~Quit(){SDL_Quit();} } quit;
    auto* window=SDL_CreateWindow("The One - Import Sandbox",1440,900,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!window)throw std::runtime_error(SDL_GetError());
    struct Window { SDL_Window* w;~Window(){SDL_DestroyWindow(w);} } windowGuard{window};
    IMGUI_CHECKVERSION();ImGui::CreateContext();theme();ImGui::GetIO().IniFilename=nullptr;
    if(!ImGui_ImplSDL3_InitForVulkan(window))throw std::runtime_error("Cannot initialize UI platform");
    struct UI {~UI(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}} ui;
    Renderer renderer;renderer.initialize(window,options.validate);
    one::InputBindings bindings(std::filesystem::path(SDL_GetBasePath())/"assets/input.json");
    Session s;auto scene=options.scene.empty()?std::filesystem::path(SDL_GetBasePath())/"assets/sandbox.one":options.scene;s.load(scene,renderer);s.selected=2;
    SDL_AudioSpec spec{};Uint8* wav=nullptr;Uint32 length=0;
    if(SDL_LoadWAV((std::filesystem::path(SDL_GetBasePath())/"assets/interact.wav").string().c_str(),&spec,&wav,&length)) {
        s.sound.assign(wav,wav+length);SDL_free(wav);s.audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
        if(s.audio)SDL_ResumeAudioStreamDevice(s.audio);else s.message(std::string("Audio unavailable: ")+SDL_GetError());
    }else s.message(std::string("Audio file unavailable: ")+SDL_GetError());
    if(options.smoke && !s.audio)throw std::runtime_error("Smoke test audio stream creation failed");
    s.message(std::string("Audio driver: ")+SDL_GetCurrentAudioDriver());
    SDL_Gamepad* gamepad=nullptr;int gamepadCount=0;auto ids=SDL_GetGamepads(&gamepadCount);if(gamepadCount>0)gamepad=SDL_OpenGamepad(ids[0]);SDL_free(ids);
    struct Pad {SDL_Gamepad*& p;~Pad(){if(p)SDL_CloseGamepad(p);}} pad{gamepad};
    double bakeMilliseconds=0;unsigned bakeSamples=0;
    bool running=true;unsigned frames=0;Uint64 previous=SDL_GetTicksNS(),restoreAt=0;float jumpCooldown=0;
    s.message("Ready: Tab walk/fly; WASD move; RMB look; Space jump; F interact; F5 reload; F6/F9 save/load game");
    while(running) {
        float mouseX=0,mouseY=0;SDL_Event event;
        auto safe=[&](const std::function<void()>& action){try{action();}catch(const std::exception& e){if(options.smoke)throw;s.message(std::string("Error: ")+e.what());}};
        while(SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if(event.type==SDL_EVENT_QUIT)running=false;
            if(event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type==SDL_EVENT_WINDOW_RESTORED)renderer.resize();
            if(event.type==SDL_EVENT_MOUSE_MOTION){mouseX+=event.motion.xrel;mouseY+=event.motion.yrel;}
            if(event.type==SDL_EVENT_DROP_FILE)safe([&]{s.import(event.drop.data);});
            if(event.type==SDL_EVENT_GAMEPAD_ADDED && !gamepad)gamepad=SDL_OpenGamepad(event.gdevice.which);
            if(event.type==SDL_EVENT_GAMEPAD_REMOVED && gamepad && SDL_GetGamepadID(gamepad)==event.gdevice.which){SDL_CloseGamepad(gamepad);gamepad=nullptr;}
            if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat && !ImGui::GetIO().WantCaptureKeyboard) {
                if(event.key.scancode==bindings.key("interact")){auto services=s.services();s.game->interact(services);}
                switch(event.key.key) {
                    case SDLK_ESCAPE:running=false;break;
                    case SDLK_TAB:s.walk=!s.walk;if(s.walk)s.simulate=true;break;
                    case SDLK_F1:s.showEditor=!s.showEditor;break;
                    case SDLK_F5:safe([&]{s.load(s.world.source,renderer);});break;
                    case SDLK_F6:safe([&]{s.saveGame();});break;
                    case SDLK_F9:safe([&]{s.loadGame(renderer);});break;
                    case SDLK_R:s.camera=one::Camera{};s.physics->resetPlayer();break;
                    default:break;
                }
            }
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse) {
                int w=0,h=0;SDL_GetWindowSize(window,&w,&h);if(w>0 && h>0)pick(s,event.button.x,event.button.y,static_cast<float>(w),static_cast<float>(h));
            }
        }
        if(!running)break;
        if(restoreAt && SDL_GetTicks()>restoreAt){SDL_RestoreWindow(window);restoreAt=0;}
        if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED){SDL_Delay(10);previous=SDL_GetTicksNS();continue;}
        auto now=SDL_GetTicksNS();float dt=std::min(static_cast<float>(now-previous)/1e9f,.2f);previous=now;
        bool focus=(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)!=0;
        const bool* keys=SDL_GetKeyboardState(nullptr);bool input=focus && !ImGui::GetIO().WantCaptureKeyboard && !options.smoke;
        auto down=[&](SDL_Scancode key){return input && keys[key];};
        bool looking=focus && !ImGui::GetIO().WantCaptureMouse && (SDL_GetMouseState(nullptr,nullptr)&SDL_BUTTON_RMASK) && !options.smoke;
        SDL_SetWindowRelativeMouseMode(window,looking);
        if(looking){s.camera.yaw-=mouseX*.003f;s.camera.pitch-=mouseY*.003f;}
        if(down(SDL_SCANCODE_LEFT))s.camera.yaw+=1.7f*dt;if(down(SDL_SCANCODE_RIGHT))s.camera.yaw-=1.7f*dt;
        if(down(SDL_SCANCODE_UP))s.camera.pitch+=1.3f*dt;if(down(SDL_SCANCODE_DOWN))s.camera.pitch-=1.3f*dt;
        float padX=0,padY=0;
        if(gamepad && input) {
            auto axis=[&](SDL_GamepadAxis a){float value=SDL_GetGamepadAxis(gamepad,a)/32767.0f;return std::abs(value)>.15f?value:0.0f;};
            padX=axis(SDL_GAMEPAD_AXIS_LEFTX);padY=-axis(SDL_GAMEPAD_AXIS_LEFTY);s.camera.yaw-=axis(SDL_GAMEPAD_AXIS_RIGHTX)*dt*2;s.camera.pitch-=axis(SDL_GAMEPAD_AXIS_RIGHTY)*dt*2;
        }
        s.camera.pitch=std::clamp(s.camera.pitch,-1.5f,1.5f);
        auto forward=s.camera.forward(),right=glm::normalize(glm::cross(forward,glm::vec3(0,1,0)));
        glm::vec3 movement=forward*(float(down(bindings.key("forward")))-float(down(bindings.key("back")))+padY)+right*(float(down(bindings.key("right")))-float(down(bindings.key("left")))+padX);
        if(s.walk)movement.y=0;else movement.y+=float(down(bindings.key("ascend")))-float(down(bindings.key("descend")));
        if(glm::length(movement)>1)movement=glm::normalize(movement);
        float speed=down(bindings.key("fast"))?8.0f:4.5f;
        if(!s.walk && !options.smoke)s.camera.position+=movement*speed*dt;
        s.desiredMovement=s.walk?movement*speed:glm::vec3(0);
        jumpCooldown=std::max(0.0f,jumpCooldown-dt);
        bool jumping=down(bindings.key("jump")) || (input && gamepad && SDL_GetGamepadButton(gamepad,SDL_GAMEPAD_BUTTON_SOUTH));
        if(jumping && jumpCooldown==0 && s.physics->grounded()){s.jump=true;jumpCooldown=.3f;s.soundEffect();}
        float simulationDt=options.smoke?1.0f/60:dt;
        if(s.simulate){s.accumulator+=simulationDt;while(s.accumulator>=1.0f/60){s.physics->step(s.world,1.0f/60,s.desiredMovement,s.jump);s.jump=false;auto services=s.services();s.game->fixedUpdate(services,1.0f/60);s.effects.step(1.0f/60);s.time+=1.0f/60;s.accumulator-=1.0f/60;}}
        if(s.walk)s.camera.position=s.physics->playerPosition()+glm::vec3(0,.6f,0);
        if(!renderer.beginFrame()){SDL_Delay(10);continue;}
        ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();if(s.showEditor)editor(s,renderer);
        auto state=s.game->state();ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x*.5f,42},ImGuiCond_Always,{.5f,0});
        ImGui::SetNextWindowBgAlpha(.75f);if(ImGui::Begin("Game HUD",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs)) {
            ImGui::Text("Collectibles: %d / %d%s",state.collected,state.total,state.completed?"   COMPLETE":"");ImGui::TextDisabled("%s | F1 editor | Space jump | F interact",s.walk?"WALK":"FLY");
        }ImGui::End();ImGui::Render();
        auto bakeStart=SDL_GetTicksNS();
        s.world.bake(s.animation?s.time:0,s.vertices,s.draws);
        s.effects.bake(s.camera,s.vertices,s.draws);
        bakeMilliseconds+=static_cast<double>(SDL_GetTicksNS()-bakeStart)/1e6;++bakeSamples;
        if(renderer.render(s.vertices,s.draws,s.camera,s.time,options.smoke && frames==110,s.shadows,s.selected))++frames;
        if(options.smoke) {
            if(frames==25)SDL_SetWindowSize(window,1200,800);
            if(frames==40){SDL_MinimizeWindow(window);restoreAt=SDL_GetTicks()+100;}
            if(frames==60){SDL_SetWindowSize(window,1440,900);s.save();}
            if(frames==70)s.load(s.savePath,renderer);
            if(frames==75)s.saveGame();
            if(frames==85)s.loadGame(renderer);
            if(frames==90)s.soundEffect();
            if(frames>=120){s.message("Smoke passed: 120 frames, texture/skin/shadow rendering, physics, save/reload, audio submission, resize and minimize/restore");running=false;}
        }
    }
    if(options.smoke)std::cout<<"Average CPU scene/effect bake: "<<bakeMilliseconds/std::max(bakeSamples,1u)<<" ms (small compatibility scene only)"<<std::endl;
    SDL_SetWindowRelativeMouseMode(window,false);renderer.close();
    if(renderer.errors())throw std::runtime_error(std::to_string(renderer.errors())+" Vulkan validation errors");
    return 0;
}

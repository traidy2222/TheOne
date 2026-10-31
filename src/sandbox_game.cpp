#include "game.hpp"
namespace one {
class SandboxGame final:public GameModule {
    GameState state_;
public:
    void start(GameServices& services) override {
        state_={}; for(auto& e:services.world.entities) if(e.name.starts_with("Collectible")) { ++state_.total; if(!e.visible)++state_.collected; }
    }
    void fixedUpdate(GameServices& services,float) override {
        auto player=services.physics.playerPosition();
        for(auto& e:services.world.entities) if(e.visible && e.name.starts_with("Collectible")) {
            auto position=glm::vec3(services.world.matrix(e.id)[3]);
            if(glm::distance(player,position)<1.6f) { e.visible=false; ++state_.collected; services.playSound(); }
        }
        state_.completed=state_.total>0 && state_.collected==state_.total;
    }
    void interact(GameServices& services) override {
        auto player=services.physics.playerPosition();
        for(const auto& e:services.world.entities) if(e.body=="dynamic" && glm::distance(player,e.transform.position)<5)
            services.physics.impulse(e.id,glm::normalize(e.transform.position-player+glm::vec3(0,.5f,0))*12.0f);
        services.playSound();
    }
    GameState state() const override { return state_; }
};
std::unique_ptr<GameModule> createSandboxGame() { return std::make_unique<SandboxGame>(); }
}

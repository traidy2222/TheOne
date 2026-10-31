#pragma once
#include "content.hpp"
#include "physics.hpp"
#include <functional>
namespace one {
struct GameServices {
    World& world; Physics& physics;
    std::function<void()> playSound;
};
struct GameState { int collected=0,total=0; bool completed=false; };
// A game's module depends on engine services and content types, never Vulkan or editor internals.
class GameModule {
public:
    virtual ~GameModule()=default;
    virtual void start(GameServices&)=0;
    virtual void fixedUpdate(GameServices&,float)=0;
    virtual void interact(GameServices&)=0;
    virtual GameState state() const=0;
};
std::unique_ptr<GameModule> createSandboxGame();
}

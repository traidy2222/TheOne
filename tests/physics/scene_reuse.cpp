#include <theone/physics/scene_reuse.hpp>
#include <theone/physics/edit.hpp>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scene fixture");
        auto current = one::PhysicsScene::load(argv[1]);
        auto ready = one::PhysicsScene::load(argv[1]);
        auto reuse = one::reuseSceneAssets(*ready, current->world);
        require(reuse.reused > 0 && reuse.replaced == 0 && reuse.obsolete.empty(),
                "Unchanged reload failed to reuse model owners");
        require(!ready->retiredModels.empty(), "Discarded fresh models lost retirement ownership");
        auto edit = one::PhysicsEdit::prepare(ready->physics->captureEdit(ready->world));
        require(edit->stats.replaced == 0 && edit->stats.added == 0,
                "Reuse lost collision model ownership");
        auto old = current->world.find(2)->model;
        require(!old->images.empty(), "Expected image fixture");
        old->images[0].rgba[0] ^= 255;
        ++old->images[0].revision;
        auto changed = one::PhysicsScene::load(argv[1]);
        reuse = one::reuseSceneAssets(*changed, current->world);
        require(reuse.replaced == 1 && reuse.reused > 0 && reuse.obsolete.size() == 1 &&
                    reuse.obsolete[0] == old && changed->world.find(2)->model != old,
                "A live image edit incorrectly survived source reload");
        auto disabled = one::PhysicsScene::load(argv[1]);
        reuse = one::reuseSceneAssets(*disabled, current->world, false);
        require(reuse.reused == 0 && reuse.replaced > 0 && !reuse.obsolete.empty(),
                "Explicit no-reuse policy was ignored");
        std::cout << "PASS unchanged ownership, retirement, collision identity, image edits and reuse policy\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

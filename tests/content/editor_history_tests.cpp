#include <theone/scene/editor_history.hpp>
using one::EditorHistory;
#include <iostream>
#include <stdexcept>
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
int main() {
    try {
        one::World world;
        for (uint64_t i = 1; i <= 257; ++i) {
            one::Entity e;
            e.id = i;
            e.body = "static";
            world.entities.push_back(e);
        }
        EditorHistory history;
        history.beginEntity(world.entities[1]);
        require(!history.commit(world).changed && history.undoCount() == 0, "No-op created history");
        history.beginEntity(world.entities[1]);
        for (int frame = 0; frame < 60; ++frame)
            world.entities[1].transform.position.x += .002f;
        auto edit = history.commit(world);
        require(edit.changed && edit.collision && history.undoCount() == 1 && history.storedEntities() == 2,
                "Gesture did not coalesce");
        world.entities[200].transform.position.y = 7;
        require(history.undo(world).collision && world.entities[1].transform.position.x == 0,
                "Undo transform");
        require(world.entities[200].transform.position.y == 7, "Undo replaced unrelated entity");
        require(history.redo(world).collision && world.entities[1].transform.position.x > .119f,
                "Redo transform");
        history.beginEntity(world.entities[1]);
        world.entities[1].visible = false;
        require(!history.commit(world).collision, "Visibility rebuilt collision");
        history.beginEntity(world.entities[1]);
        ++world.entities[1].clip;
        require(history.commit(world).collision, "Static clip missed collision rebuild");
        world.entities[2].body = "dynamic";
        history.beginEntity(world.entities[2]);
        ++world.entities[2].clip;
        require(!history.commit(world).collision, "Dynamic animation rebuilt collision");
        history.begin(world);
        auto duplicate = world.entities[1];
        duplicate.id = 258;
        world.entities.push_back(duplicate);
        require(history.commit(world).collision, "Creation missed collision");
        history.undo(world);
        require(world.entities.size() == 257, "Undo creation");
        history.redo(world);
        require(world.entities.size() == 258 && world.entities.back().id == 258, "Redo creation");
        auto old = std::make_shared<one::Model>(), replacement = std::make_shared<one::Model>();
        world.entities[0].model = world.entities[1].model = old;
        history.begin(world);
        world.entities[0].model = world.entities[1].model = replacement;
        history.commit(world);
        history.undo(world);
        require(world.entities[0].model == old && world.entities[1].model == old, "Bulk reload undo");
        history.redo(world);
        require(world.entities[0].model == replacement, "Bulk reload redo");
        for (int i = 0; i < 40; ++i) {
            history.beginEntity(world.entities[0]);
            world.entities[0].name = std::to_string(i);
            history.commit(world);
        }
        require(history.undoCount() == 32 && history.storedEntities() == 64,
                "History grew beyond entity delta limit");
        std::cout << "PASS no-op, gesture coalescing, collision classification, undo/redo isolation, "
                     "creation, bulk reload, bounded history\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

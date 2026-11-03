#pragma once
#include <theone/scene/world.hpp>
#include <optional>
namespace one {
// Entity-delta transactions shared by editor applications.
struct EditResult {
    bool changed = false, collision = false;
};
class EditorHistory {
    struct Delta {
        uint64_t id;
        size_t index;
        std::optional<one::Entity> before, after;
    };
    struct Command {
        std::vector<Delta> changes;
        bool collision = false;
    };
    std::vector<Command> undo_, redo_;
    std::vector<one::Entity> before_;
    bool pending_ = false, all_ = false;
    static bool same(const one::Entity& a, const one::Entity& b);
    static bool collision(const std::optional<one::Entity>& a, const std::optional<one::Entity>& b);
    static void apply(one::World& world, const Command& command, bool back);

  public:
    void begin(const one::World& world);
    void beginEntity(const one::Entity& entity);
    EditResult commit(const one::World& world);
    EditResult undo(one::World& world);
    EditResult redo(one::World& world);
    bool pending() const { return pending_; }
    size_t undoCount() const { return undo_.size(); }
    size_t storedEntities() const;
};

} // namespace one

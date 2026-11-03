#include <theone/scene/editor_history.hpp>
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
namespace one {
namespace {
bool transformSame(const one::Transform& a,const one::Transform& b) {
    return a.position==b.position && a.rotation==b.rotation && a.scale==b.scale;
}
}
bool EditorHistory::same(const one::Entity& a,const one::Entity& b) {
    return transformSame(a.transform,b.transform) && a.id==b.id && a.parent==b.parent && a.name==b.name &&
        a.asset==b.asset && a.model==b.model && a.visible==b.visible && a.animate==b.animate && a.clip==b.clip &&
        a.body==b.body && a.halfExtent==b.halfExtent && a.mass==b.mass && a.behavior==b.behavior && a.speed==b.speed;
}
bool EditorHistory::collision(const std::optional<one::Entity>& a,const std::optional<one::Entity>& b) {
    if(!a)return b && b->body!="none";
    if(!b)return a->body!="none";
    if(a->body=="none" && b->body=="none")return false;
    return !transformSame(a->transform,b->transform) || a->body!=b->body || a->model!=b->model ||
        a->halfExtent!=b->halfExtent || a->mass!=b->mass || a->parent!=b->parent ||
        (a->body=="static" && a->clip!=b->clip);
}
void EditorHistory::begin(const one::World& world) {
    if(pending_)throw std::runtime_error("Finish current editor transaction first");
    before_=world.entities;all_=true;pending_=true;
}
void EditorHistory::beginEntity(const one::Entity& entity) {
    if(pending_)throw std::runtime_error("Finish current editor transaction first");
    before_={entity};all_=false;pending_=true;
}
EditResult EditorHistory::commit(const one::World& world) {
    if(!pending_)return {};
    Command command;
    std::unordered_map<uint64_t,const one::Entity*> old;
    for(const auto& e:before_)old[e.id]=&e;
    for(size_t i=0;i<world.entities.size();++i) {
        const auto& e=world.entities[i];auto it=old.find(e.id);
        if(it==old.end()) {if(all_)command.changes.push_back({e.id,i,{},e});}
        else {if(!same(*it->second,e))command.changes.push_back({e.id,i,*it->second,e});old.erase(it);}
    }
    for(size_t i=0;i<before_.size();++i)if(old.contains(before_[i].id))
        command.changes.push_back({before_[i].id,i,before_[i],{}});
    pending_=false;before_.clear();
    if(command.changes.empty())return {};
    for(const auto& d:command.changes)command.collision|=collision(d.before,d.after);
    auto needsCollision=command.collision;
    undo_.push_back(std::move(command));if(undo_.size()>32)undo_.erase(undo_.begin());redo_.clear();
    return {true,needsCollision};
}
void EditorHistory::apply(one::World& world,const Command& command,bool back) {
    for(const auto& d:command.changes) {
        const auto& value=back?d.before:d.after;
        auto it=std::find_if(world.entities.begin(),world.entities.end(),[&](const auto& e){return e.id==d.id;});
        if(value) {
            if(it==world.entities.end())world.entities.insert(world.entities.begin()+std::min(d.index,world.entities.size()),*value);
            else *it=*value;
        } else if(it!=world.entities.end())world.entities.erase(it);
    }
}
EditResult EditorHistory::undo(one::World& world) {
    if(pending_ || undo_.empty())return {};
    auto command=std::move(undo_.back());undo_.pop_back();apply(world,command,true);
    auto needsCollision=command.collision;redo_.push_back(std::move(command));return {true,needsCollision};
}
EditResult EditorHistory::redo(one::World& world) {
    if(pending_ || redo_.empty())return {};
    auto command=std::move(redo_.back());redo_.pop_back();apply(world,command,false);
    auto needsCollision=command.collision;undo_.push_back(std::move(command));return {true,needsCollision};
}
size_t EditorHistory::storedEntities() const {
    size_t count=0;for(const auto* stack:{&undo_,&redo_})for(const auto& command:*stack)
        for(const auto& d:command.changes)count+=bool(d.before)+bool(d.after);
    return count;
}

} // namespace one

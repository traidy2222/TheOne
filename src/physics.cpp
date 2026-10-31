#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include "physics.hpp"
#include <unordered_map>
#include <stdexcept>
namespace one {
namespace {
JPH::Vec3 v(glm::vec3 p) { return {p.x,p.y,p.z}; }
glm::vec3 v(JPH::Vec3Arg p) { return {p.GetX(),p.GetY(),p.GetZ()}; }
class Broad final:public JPH::BroadPhaseLayerInterface {
public: JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(l)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override { return l.GetValue()==0?"Static":"Moving"; }
#endif
};
class ObjectBroad final:public JPH::ObjectVsBroadPhaseLayerFilter { public: bool ShouldCollide(JPH::ObjectLayer a,JPH::BroadPhaseLayer b) const override { return a!=0 || b.GetValue()!=0; } };
class Pair final:public JPH::ObjectLayerPairFilter { public: bool ShouldCollide(JPH::ObjectLayer a,JPH::ObjectLayer b) const override { return a!=0 || b!=0; } };
class NotPlayer final:public JPH::BodyFilter { JPH::BodyID player_; public: explicit NotPlayer(JPH::BodyID p):player_(p){} bool ShouldCollide(const JPH::BodyID& id) const override { return id!=player_; } };
}
struct Physics::Impl {
    Broad broad; ObjectBroad objectBroad; Pair pair;
    JPH::TempAllocatorImpl allocator{32*1024*1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs,JPH::cMaxPhysicsBarriers,1};
    JPH::PhysicsSystem system;
    JPH::BodyID player;
    std::unordered_map<uint64_t,JPH::BodyID> bodies;
    bool onGround=false;
    Impl() { system.Init(8192,0,32768,8192,broad,objectBroad,pair); }
    void clear() {
        auto& bi=system.GetBodyInterface();
        if(!player.IsInvalid()) { bi.RemoveBody(player); bi.DestroyBody(player); player=JPH::BodyID(); }
        for(const auto& [id,body]:bodies) { (void)id; bi.RemoveBody(body); bi.DestroyBody(body); }
        bodies.clear();
    }
    ~Impl() { clear(); }
};
static unsigned users=0;
Physics::Physics() {
    if(users++==0) { JPH::RegisterDefaultAllocator(); JPH::Factory::sInstance=new JPH::Factory(); JPH::RegisterTypes(); }
    impl_=std::make_unique<Impl>();
}
Physics::~Physics() { impl_.reset(); if(--users==0) { JPH::UnregisterTypes(); delete JPH::Factory::sInstance; JPH::Factory::sInstance=nullptr; } }
void Physics::rebuild(const World& world) {
    impl_->clear(); auto& bi=impl_->system.GetBodyInterface();
    for(const auto& e:world.entities) if(e.body!="none") {
        JPH::RefConst<JPH::Shape> shape;
        if(e.body=="static" && e.model) {
            std::vector<Vertex> vertices; std::vector<Draw> draws;
            e.model->bake(world.matrix(e.id),0,e.clip,false,e.id,vertices,draws);
            JPH::TriangleList triangles; triangles.reserve(vertices.size()/3);
            for(size_t i=0;i<vertices.size();i+=3) triangles.emplace_back(v(vertices[i].position),v(vertices[i+1].position),v(vertices[i+2].position));
            if(triangles.empty()) continue;
            auto result=JPH::MeshShapeSettings(triangles).Create();
            if(result.HasError()) throw std::runtime_error(result.GetError().c_str()); shape=result.Get();
            JPH::BodyCreationSettings settings(shape,JPH::RVec3::sZero(),JPH::Quat::sIdentity(),JPH::EMotionType::Static,0);
            auto id=bi.CreateAndAddBody(settings,JPH::EActivation::DontActivate); if(id.IsInvalid()) throw std::runtime_error("Physics body capacity exceeded"); impl_->bodies[e.id]=id;
        } else {
            shape=new JPH::BoxShape(v(e.halfExtent*e.transform.scale));
            auto q=e.transform.rotation;
            JPH::BodyCreationSettings settings(shape,JPH::RVec3(e.transform.position.x,e.transform.position.y,e.transform.position.z),JPH::Quat(q.x,q.y,q.z,q.w),e.body=="dynamic"?JPH::EMotionType::Dynamic:JPH::EMotionType::Static,e.body=="dynamic"?1:0);
            settings.mOverrideMassProperties=JPH::EOverrideMassProperties::CalculateInertia; settings.mMassPropertiesOverride.mMass=e.mass;
            auto id=bi.CreateAndAddBody(settings,e.body=="dynamic"?JPH::EActivation::Activate:JPH::EActivation::DontActivate);
            if(id.IsInvalid()) throw std::runtime_error("Physics body capacity exceeded"); impl_->bodies[e.id]=id;
        }
    }
    JPH::BodyCreationSettings player(new JPH::CapsuleShape(0.5f,0.3f),JPH::RVec3(0,2,10),JPH::Quat::sIdentity(),JPH::EMotionType::Dynamic,1);
    player.mAllowedDOFs=JPH::EAllowedDOFs::TranslationX|JPH::EAllowedDOFs::TranslationY|JPH::EAllowedDOFs::TranslationZ;
    player.mFriction=0; player.mLinearDamping=0; player.mMotionQuality=JPH::EMotionQuality::LinearCast;
    impl_->player=bi.CreateAndAddBody(player,JPH::EActivation::Activate);
    if(impl_->player.IsInvalid()) throw std::runtime_error("Cannot create player body");
    impl_->system.OptimizeBroadPhase();
}
void Physics::step(World& world,float dt,glm::vec3 movement,bool jump) {
    auto& bi=impl_->system.GetBodyInterface();
    if(!impl_->player.IsInvalid()) {
        auto p=bi.GetCenterOfMassPosition(impl_->player);
        JPH::RayCastResult hit;
        impl_->onGround=impl_->system.GetNarrowPhaseQuery().CastRay(JPH::RRayCast(p,JPH::Vec3(0,-0.92f,0)),hit,{}, {},NotPlayer(impl_->player));
        auto velocity=bi.GetLinearVelocity(impl_->player);
        bi.SetLinearVelocity(impl_->player,JPH::Vec3(movement.x,jump && impl_->onGround?5.3f:velocity.GetY(),movement.z));
    }
    auto result=impl_->system.Update(dt,1,&impl_->allocator,&impl_->jobs);
    if(result!=JPH::EPhysicsUpdateError::None) throw std::runtime_error("Physics update capacity exceeded");
    for(const auto& [id,body]:impl_->bodies) if(auto e=world.find(id);e && e->body=="dynamic") {
        e->transform.position=v(bi.GetPosition(body)); auto q=bi.GetRotation(body); e->transform.rotation={q.GetW(),q.GetX(),q.GetY(),q.GetZ()};
    }
    if(playerPosition().y < -30) resetPlayer();
}
glm::vec3 Physics::playerPosition() const { return v(impl_->system.GetBodyInterface().GetPosition(impl_->player)); }
bool Physics::grounded() const { return impl_->onGround; }
void Physics::resetPlayer(glm::vec3 position) { if(!impl_->player.IsInvalid()) { auto& bi=impl_->system.GetBodyInterface(); bi.SetPosition(impl_->player,v(position),JPH::EActivation::Activate); bi.SetLinearVelocity(impl_->player,JPH::Vec3::sZero()); } }
void Physics::impulse(uint64_t id,glm::vec3 impulse) { auto it=impl_->bodies.find(id); if(it!=impl_->bodies.end()) impl_->system.GetBodyInterface().AddImpulse(it->second,v(impulse)); }
std::vector<BodyMotion> Physics::motions()const {
    std::vector<BodyMotion> result;const auto& bi=impl_->system.GetBodyInterface();
    for(const auto& [id,body]:impl_->bodies)result.push_back({id,v(bi.GetLinearVelocity(body)),v(bi.GetAngularVelocity(body))});return result;
}
void Physics::restoreMotions(const std::vector<BodyMotion>& motions) {
    auto& bi=impl_->system.GetBodyInterface();for(const auto& motion:motions){auto it=impl_->bodies.find(motion.id);if(it!=impl_->bodies.end() && bi.GetMotionType(it->second)==JPH::EMotionType::Dynamic)bi.SetLinearAndAngularVelocity(it->second,v(motion.linear),v(motion.angular));}
}
glm::vec3 Physics::playerVelocity()const{return v(impl_->system.GetBodyInterface().GetLinearVelocity(impl_->player));}
void Physics::setPlayerVelocity(glm::vec3 velocity){impl_->system.GetBodyInterface().SetLinearVelocity(impl_->player,v(velocity));}

}

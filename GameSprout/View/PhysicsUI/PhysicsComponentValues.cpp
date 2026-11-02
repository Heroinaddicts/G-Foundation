#include "PhysicsComponentValues.h"

namespace PhysicsComponentValues {
namespace {
bool Equal(GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b){return a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;}
bool Shape(GameSproutRuntime::IColliderComponent& collider,const Collider& value) {
    if(value.Kind==ShapeKind::Box){auto* typed=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(&collider);return typed&&typed->SetShape(value.Box);}
    if(value.Kind==ShapeKind::Sphere){auto* typed=dynamic_cast<GameSproutRuntime::ISphereColliderComponent*>(&collider);return typed&&typed->SetShape(value.Sphere);}
    auto* typed=dynamic_cast<GameSproutRuntime::ICapsuleColliderComponent*>(&collider);return typed&&typed->SetShape(value.Capsule);
}
}
bool Read(const GameSproutRuntime::IColliderComponent& collider,Collider& value) {
    value.Settings=collider.GetColliderSettings();
    if(const auto* typed=dynamic_cast<const GameSproutRuntime::IBoxColliderComponent*>(&collider)){value.Kind=ShapeKind::Box;value.Box=typed->GetShape();return true;}
    if(const auto* typed=dynamic_cast<const GameSproutRuntime::ISphereColliderComponent*>(&collider)){value.Kind=ShapeKind::Sphere;value.Sphere=typed->GetShape();return true;}
    if(const auto* typed=dynamic_cast<const GameSproutRuntime::ICapsuleColliderComponent*>(&collider)){value.Kind=ShapeKind::Capsule;value.Capsule=typed->GetShape();return true;}
    return false;
}
bool Write(GameSproutRuntime::IColliderComponent& collider,const Collider& value,std::string& error) {
    Collider previous;if(!Read(collider,previous)){error="不支持的碰撞器类型。";return false;}
    if(Shape(collider,value)&&collider.SetColliderSettings(value.Settings))return true;
    Shape(collider,previous);collider.SetColliderSettings(previous.Settings);
    error="碰撞器设置无效。尺寸和半径须为正，胶囊总高度不能小于直径，碰撞层须在 0..31。";return false;
}
bool Equal(const Collider& a,const Collider& b) {
    const auto& x=a.Settings;const auto& y=b.Settings;
    if(a.Kind!=b.Kind||x.Enabled!=y.Enabled||x.IsTrigger!=y.IsTrigger||!Equal(x.Center,y.Center)||x.Material!=y.Material||x.CollisionLayer!=y.CollisionLayer||x.CollisionMask!=y.CollisionMask||
       x.Rotation.X!=y.Rotation.X||x.Rotation.Y!=y.Rotation.Y||x.Rotation.Z!=y.Rotation.Z||x.Rotation.W!=y.Rotation.W)return false;
    if(a.Kind==ShapeKind::Box)return Equal(a.Box.Size,b.Box.Size);
    if(a.Kind==ShapeKind::Sphere)return a.Sphere.Radius==b.Sphere.Radius;
    return a.Capsule.Radius==b.Capsule.Radius&&a.Capsule.Height==b.Capsule.Height&&a.Capsule.Axis==b.Capsule.Axis;
}
bool Equal(const GameSproutRuntime::RigidbodySettings& a,const GameSproutRuntime::RigidbodySettings& b) {
    return a.Enabled==b.Enabled&&a.MotionType==b.MotionType&&a.Mass==b.Mass&&a.GravityFactor==b.GravityFactor&&
        a.LinearDamping==b.LinearDamping&&a.AngularDamping==b.AngularDamping&&a.Constraints==b.Constraints&&a.Continuous==b.Continuous;
}
GameSproutRuntime::IComponent* Resolve(GameSproutRuntime::IGameObject& object,ISceneManager* scenes,uint64_t fileID,uint64_t liveID) {
    if(fileID&&scenes)return scenes->QueryComponentByFileID(object.GetInstanceID(),fileID);
    for(std::size_t index=0;index<object.GetComponentCount();++index){auto* component=object.GetComponentAt(index);if(component&&component->GetComponentInstanceID()==liveID)return component;}
    return nullptr;
}
}

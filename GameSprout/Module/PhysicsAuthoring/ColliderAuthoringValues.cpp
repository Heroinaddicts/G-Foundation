#include "ColliderAuthoringValues.h"

namespace ColliderAuthoringValues {
namespace {
bool Equal(GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b){return a.X==b.X&&a.Y==b.Y&&a.Z==b.Z;}
bool Shape(GameSproutRuntime::IColliderComponent& collider,const ColliderEditState& value) {
    if(value.Kind==ColliderShapeKind::Box){auto* typed=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(&collider);return typed&&typed->SetShape(value.Box);}
    if(value.Kind==ColliderShapeKind::Sphere){auto* typed=dynamic_cast<GameSproutRuntime::ISphereColliderComponent*>(&collider);return typed&&typed->SetShape(value.Sphere);}
    if(value.Kind==ColliderShapeKind::Capsule){auto* typed=dynamic_cast<GameSproutRuntime::ICapsuleColliderComponent*>(&collider);return typed&&typed->SetShape(value.Capsule);}
    auto* typed=dynamic_cast<GameSproutRuntime::IMeshColliderComponent*>(&collider);return typed&&typed->SetShape(value.Mesh);
}
}
bool Read(const GameSproutRuntime::IColliderComponent& collider,ColliderEditState& value) {
    value={};value.Settings=collider.GetColliderSettings();
    if(auto* typed=dynamic_cast<const GameSproutRuntime::IBoxColliderComponent*>(&collider)){value.Kind=ColliderShapeKind::Box;value.Box=typed->GetShape();}
    else if(auto* typed=dynamic_cast<const GameSproutRuntime::ISphereColliderComponent*>(&collider)){value.Kind=ColliderShapeKind::Sphere;value.Sphere=typed->GetShape();}
    else if(auto* typed=dynamic_cast<const GameSproutRuntime::ICapsuleColliderComponent*>(&collider)){value.Kind=ColliderShapeKind::Capsule;value.Capsule=typed->GetShape();}
    else if(auto* typed=dynamic_cast<const GameSproutRuntime::IMeshColliderComponent*>(&collider)){value.Kind=ColliderShapeKind::Mesh;value.Mesh=typed->GetShape();}
    else return false;
    return true;
}
bool Equal(const ColliderEditState& a,const ColliderEditState& b) {
    const auto& x=a.Settings;const auto& y=b.Settings;
    if(a.Kind!=b.Kind||x.Enabled!=y.Enabled||x.IsTrigger!=y.IsTrigger||!Equal(x.Center,y.Center)||x.Material!=y.Material||
       x.Rotation.X!=y.Rotation.X||x.Rotation.Y!=y.Rotation.Y||x.Rotation.Z!=y.Rotation.Z||x.Rotation.W!=y.Rotation.W)return false;
    if(a.Kind==ColliderShapeKind::Box)return Equal(a.Box.Size,b.Box.Size);
    if(a.Kind==ColliderShapeKind::Sphere)return a.Sphere.Radius==b.Sphere.Radius;
    if(a.Kind==ColliderShapeKind::Capsule)return a.Capsule.Radius==b.Capsule.Radius&&a.Capsule.Height==b.Capsule.Height&&a.Capsule.Axis==b.Capsule.Axis;
    return a.Mesh.Mesh==b.Mesh.Mesh&&a.Mesh.Convex==b.Mesh.Convex;
}
bool Write(GameSproutRuntime::IColliderComponent& collider,const ColliderEditState& value,std::string& error) {
    error.clear();ColliderEditState before;
    if(!Read(collider,before)||before.Kind!=value.Kind){error="碰撞体类型已经改变。";return false;}
    auto safe=before.Settings;safe.Enabled=false;safe.IsTrigger=false;
    if(collider.SetColliderSettings(safe)&&Shape(collider,value)&&collider.SetColliderSettings(value.Settings))return true;
    collider.SetColliderSettings(safe);Shape(collider,before);collider.SetColliderSettings(before.Settings);
    error="Runtime 拒绝碰撞体参数；尺寸须为正，胶囊高度不能小于直径。";return false;
}
ISceneManager::fSceneEditOperation Operation(ISceneManager* scenes,std::uint64_t fileID,std::uint64_t liveID,const ColliderEditState& value) {
    return [scenes,fileID,liveID,value](GameSproutRuntime::IGameObject& object,std::string& error) {
        GameSproutRuntime::IComponent* component=nullptr;
        if(fileID&&scenes)component=scenes->QueryComponentByFileID(object.GetInstanceID(),fileID);
        else for(std::size_t i=0;i<object.GetComponentCount();++i)if(auto* item=object.GetComponentAt(i);item&&item->GetComponentInstanceID()==liveID){component=item;break;}
        auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(component);
        if(!collider){error="碰撞体已经移除。";return false;}
        return Write(*collider,value,error);
    };
}
}

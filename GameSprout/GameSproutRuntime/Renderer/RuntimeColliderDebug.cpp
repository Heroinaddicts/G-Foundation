#include "RuntimeRenderer.h"
#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Physics/PhysicsConversions.h"
#include "Physics/MeshColliderGeometry.h"
#include "Api/RuntimeColliderMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <set>

namespace GameSproutRuntime {
namespace {
constexpr std::size_t DebugVertexLimit=1048576;
constexpr unsigned CircleSteps=32;
constexpr float Tau=6.2831853071795864769f;
struct ColliderQueryScope {
    bool& Busy;
    explicit ColliderQueryScope(bool& busy):Busy(busy){Busy=true;}
    ~ColliderQueryScope(){Busy=false;}
};
Vector3 CapsuleAxis(Vector3 value,PhysicsAxis axis) {
    if(axis==PhysicsAxis::X)return {value.Y,-value.X,value.Z};
    if(axis==PhysicsAxis::Z)return {value.X,-value.Z,value.Y};
    return value;
}
class ColliderLines {
public:
    explicit ColliderLines(std::size_t available):Available(available){}
    std::vector<DebugLine> Lines;
    Mat4 Transform;
    Color Tint;
    bool DepthTest=false;
    bool Exhausted=false;
    bool Add(Vector3 a,Vector3 b) {
        if(Lines.size()>=Available){Exhausted=true;return false;}
        const auto from=Transform*Vector4{a.X,a.Y,a.Z,1},to=Transform*Vector4{b.X,b.Y,b.Z,1};
        if(!RuntimeMath::IsFinite(Vector3{from.X,from.Y,from.Z})||!RuntimeMath::IsFinite(Vector3{to.X,to.Y,to.Z}))return false;
        Lines.push_back({{from.X,from.Y,from.Z},{to.X,to.Y,to.Z},Tint,DepthTest});return true;
    }
    bool Circle(float radius,float y=0,PhysicsAxis axis=PhysicsAxis::Y) {
        for(unsigned step=0;step<CircleSteps;++step) {
            const float a=Tau*step/CircleSteps,b=Tau*(step+1)/CircleSteps;
            if(!Add(CapsuleAxis({radius*std::cos(a),y,radius*std::sin(a)},axis),
                    CapsuleAxis({radius*std::cos(b),y,radius*std::sin(b)},axis)))return false;
        }
        return true;
    }
    bool Box(Vector3 half) {
        const std::array<Vector3,8> points={Vector3{-half.X,-half.Y,-half.Z},{half.X,-half.Y,-half.Z},
            {half.X,half.Y,-half.Z},{-half.X,half.Y,-half.Z},{-half.X,-half.Y,half.Z},
            {half.X,-half.Y,half.Z},{half.X,half.Y,half.Z},{-half.X,half.Y,half.Z}};
        constexpr unsigned edges[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for(const auto& edge:edges)if(!Add(points[edge[0]],points[edge[1]]))return false;
        return true;
    }
    bool Sphere(float radius) {
        return Circle(radius,0,PhysicsAxis::X)&&Circle(radius)&&Circle(radius,0,PhysicsAxis::Z);
    }
    bool Capsule(float radius,float halfCylinder,PhysicsAxis axis) {
        if(halfCylinder<=0)return Sphere(radius);
        if(!Circle(radius,halfCylinder,axis)||!Circle(radius,-halfCylinder,axis))return false;
        for(unsigned meridian=0;meridian<4;++meridian) {
            const float angle=Tau*meridian/4;
            const Vector3 radial{std::cos(angle),0,std::sin(angle)};
            if(!Add(CapsuleAxis(radial*radius+Vector3{0,halfCylinder,0},axis),
                    CapsuleAxis(radial*radius-Vector3{0,halfCylinder,0},axis)))return false;
            for(unsigned step=0;step<CircleSteps/4;++step) {
                const float a=Tau*step/CircleSteps,b=Tau*(step+1)/CircleSteps;
                for(float side:{-1.0f,1.0f}) {
                    const Vector3 first=radial*(radius*std::cos(a))+Vector3{0,side*(halfCylinder+radius*std::sin(a)),0};
                    const Vector3 second=radial*(radius*std::cos(b))+Vector3{0,side*(halfCylinder+radius*std::sin(b)),0};
                    if(!Add(CapsuleAxis(first,axis),CapsuleAxis(second,axis)))return false;
                }
            }
        }
        return true;
    }
private:
    std::size_t Available;
};
bool SelectedCollider(IGameObject* object,IGameObject* selected,bool children) {
    if(!selected)return false;
    if(object==selected)return true;
    if(children)for(auto* parent=object->Parent();parent;parent=parent->Parent())if(parent==selected)return true;
    return false;
}
}

Renderer::ColliderMeshWire* Renderer::ColliderMesh(IColliderComponent& collider,const ResourceReference& reference,
                                                  bool convex,const Mat4& relative) {
    const auto key=std::make_pair(collider.GetGameObject()->GetInstanceID(),collider.GetComponentInstanceID());
    auto [found,inserted]=_ColliderMeshes.try_emplace(key);
    auto& cached=found->second;
    if(inserted||cached.Reference!=reference||cached.Convex!=convex||!PhysicsNear(cached.Relative,relative)) {
        cached={};cached.Reference=reference;cached.Convex=convex;cached.Relative=relative;
        MeshGPU* mesh=Mesh(reference);
        if(!mesh){
            const auto missing=_Meshes.find(ResourceKey(reference));
            cached.Error=missing!=_Meshes.end()&&!missing->second.Error.empty()?missing->second.Error:"Collider mesh is missing";
        }
        else {
            GPhysics::MeshGeometry source,transformed,cooked;
            source.Vertices.reserve(mesh->Positions.size());
            for(const auto point:mesh->Positions)source.Vertices.push_back(PhysicsVector(point));
            source.Indices=mesh->Indices;
            if(TransformMeshColliderGeometry(source,relative,transformed,cached.Error)
                &&GPhysics::CookMeshGeometry(transformed,convex,cooked,cached.Error)) {
                std::set<std::pair<std::uint32_t,std::uint32_t>> edges;
                for(std::size_t triangle=0;triangle<cooked.Indices.size();triangle+=3) {
                    for(unsigned edge=0;edge<3;++edge) {
                        const auto first=cooked.Indices[triangle+edge],second=cooked.Indices[triangle+(edge+1)%3];
                        const auto pair=std::minmax(first,second);
                        if(first!=second&&edges.emplace(pair.first,pair.second).second)
                            cached.Edges.emplace_back(RuntimeVector(cooked.Vertices[first]),RuntimeVector(cooked.Vertices[second]));
                    }
                }
            }
        }
    }
    if(!cached.Error.empty()){Diagnostic(cached.Error);return nullptr;}
    return &cached;
}

bool Renderer::AppendColliderWireframes(IScene* scene,const ColliderDebugOptions& options,DebugDrawList& draw) {
    if(!BeginQuery(scene))return false;
    if(draw.Lines.size()>DebugVertexLimit/2||draw.Triangles.size()>DebugVertexLimit/3
       ||draw.Lines.size()*2+draw.Triangles.size()*3>DebugVertexLimit)return Fail("Existing debug draw exceeds vertex limit");
    ColliderQueryScope query(_Rendering);RuntimeOperation operation(_Runtime);
    try {
        SyncRevision();
        IGameObject* selected=nullptr;
        if(options.SelectedObjectID) {
            selected=_Runtime.QueryGameObject(options.SelectedObjectID);
            if(!selected||selected->GetScene()!=scene)return Fail("Collider debug selection is not in the queried scene");
        }
        if(!options.DrawAll&&!selected)return true;
        // Cached geometry owns no nodes or component pointers. Drop identities
        // when their object disappears; resource revision drops all entries.
        for(auto found=_ColliderMeshes.begin();found!=_ColliderMeshes.end();) {
            if(!_Runtime.Find(found->first.first))found=_ColliderMeshes.erase(found);else ++found;
        }
        ColliderLines builder((DebugVertexLimit-draw.Lines.size()*2-draw.Triangles.size()*3)/2);
        builder.DepthTest=options.DepthTest;
        std::vector<IGameObject*> pending{options.DrawAll?scene:selected};
        while(!pending.empty()) {
            auto* object=pending.back();pending.pop_back();
            if(options.DrawAll||options.IncludeChildren)
                for(std::size_t child=object->GetChildCount();child>0;--child)pending.push_back(object->GetChild(child-1));
            const bool highlighted=SelectedCollider(object,selected,options.IncludeChildren);
            for(std::size_t index=0;index<object->GetComponentCount();++index) {
                auto* collider=dynamic_cast<IColliderComponent*>(object->GetComponentAt(index));
                if(!collider)continue;
                const auto& settings=collider->GetColliderSettings();
                const bool enabled=settings.Enabled&&object->IsActiveInHierarchy();
                if(!highlighted&&(!options.DrawAll||!enabled))continue;
                builder.Tint=!enabled?Color{.55f,.6f,.65f,.7f}:settings.IsTrigger?Color{.25f,.85f,1,1}:
                    highlighted?Color{.55f,1,.35f,1}:Color{.35f,.8f,.45f,.8f};
                const Mat4 colliderWorld=object->GetTransform()->GetWorldMatrix()
                    *RuntimeMath::ComposeTRS(settings.Center,settings.Rotation,{1,1,1});
                std::string error;GPhysics::Pose pose;Vector3 scale;
                const auto before=builder.Lines.size();
                bool appended=false;
                if(auto* mesh=dynamic_cast<IMeshColliderComponent*>(collider)) {
                    const auto shape=mesh->GetShape();
                    if(shape.Mesh.Guid.empty()||!shape.Mesh.LocalID)continue;
                    auto* body=collider->GetAttachedRigidbody();
                    const auto bodyWorld=(body?body->GetGameObject():object)->GetTransform()->GetWorldMatrix();
                    if(!PhysicsBodyPose(bodyWorld,pose,scale,!body,error)){Diagnostic(object->GetName()+": "+error);continue;}
                    builder.Transform=RuntimeMath::ComposeTRS(RuntimeVector(pose.Position),RuntimeRotation(pose.Rotation),{1,1,1});
                    Mat4 inverse;if(!RuntimeMath::TryInverseAffine(builder.Transform,inverse)){Diagnostic(object->GetName()+": cannot invert collider debug body pose");continue;}
                    auto* wire=ColliderMesh(*collider,shape.Mesh,shape.Convex,inverse*colliderWorld);
                    if(!wire)continue;
                    appended=true;for(const auto& edge:wire->Edges)if(!builder.Add(edge.first,edge.second)){appended=false;break;}
                }else {
                    RuntimePhysicsMath::ColliderFrame frame;
                    const auto primitiveWorld=dynamic_cast<ISphereColliderComponent*>(collider)?
                        object->GetTransform()->GetWorldMatrix()*RuntimeMath::ComposeTRS(settings.Center,Quaternion{},{1,1,1}):colliderWorld;
                    if(!RuntimePhysicsMath::TryGetColliderFrame(primitiveWorld,frame)) {
                        Diagnostic(object->GetName()+": primitive collider requires non-zero orthogonal TRS (no shear)");continue;
                    }
                    scale=RuntimePhysicsMath::AbsoluteScale(frame.Scale);
                    builder.Transform=RuntimeMath::ComposeTRS(frame.Position,frame.Rotation,{1,1,1});
                    if(auto* box=dynamic_cast<IBoxColliderComponent*>(collider)) {
                        const auto shape=box->GetShape();
                        appended=builder.Box({shape.Size.X*scale.X*.5f,shape.Size.Y*scale.Y*.5f,shape.Size.Z*scale.Z*.5f});
                    }else if(auto* sphere=dynamic_cast<ISphereColliderComponent*>(collider)) {
                        appended=builder.Sphere(sphere->GetShape().Radius*RuntimePhysicsMath::SphereScale(scale));
                    }else if(auto* capsule=dynamic_cast<ICapsuleColliderComponent*>(collider)) {
                        const auto shape=capsule->GetShape();
                        const float radius=shape.Radius*RuntimePhysicsMath::CapsuleRadiusScale(scale,shape.Axis);
                        const float height=std::max(shape.Height*RuntimePhysicsMath::CapsuleHeightScale(scale,shape.Axis),radius*2);
                        appended=builder.Capsule(radius,height*.5f-radius,shape.Axis);
                    }else continue;
                }
                if(!appended) {
                    if(builder.Exhausted)return Fail("Collider wireframe exceeds debug vertex limit");
                    builder.Lines.resize(before);Diagnostic(object->GetName()+": collider wireframe has nonfinite world coordinates");
                }
            }
        }
        draw.Lines.reserve(draw.Lines.size()+builder.Lines.size());
        draw.Lines.insert(draw.Lines.end(),builder.Lines.begin(),builder.Lines.end());return true;
    }catch(const std::exception& error){return Fail(std::string("Runtime collider debug: ")+error.what());}
    catch(...){return Fail("Unexpected Runtime collider debug failure");}
}
}

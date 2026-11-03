#include "Api/RuntimeResourceApi.h"
#include "Components/TransformComponent.h"
#include "GameObject.h"
#include "Scene.h"
#include "PhysicsConversions.h"
#include "RuntimePhysicsSystem.h"
#include "Api/RuntimeColliderMath.h"
#include <algorithm>
#include <set>

namespace GameSproutRuntime {
void RuntimePhysicsSystem::SyncResources() {
    auto* provider=_Runtime.GetResourceProvider();
    const auto revision=provider?provider->GetRevision():0;
    if(provider!=_Provider) {
        _Provider=provider;_ProviderRevision=revision;
        _Materials.clear();_MeshSources.clear();_MeshShapes.clear();_ResourceRevisions.clear();
        ++_ResourceGeneration;
        return;
    }
    if(revision==_ProviderRevision)return;
    _ProviderRevision=revision;
    bool changed=false;
    for(auto found=_ResourceRevisions.begin();found!=_ResourceRevisions.end();) {
        const ResourceReference reference{found->first.first,found->first.second};
        if((provider?provider->GetResourceRevision(reference):0)==found->second) {++found;continue;}
        _Materials.erase(found->first);
        _MeshSources.erase(found->first);
        for(auto shape=_MeshShapes.begin();shape!=_MeshShapes.end();) {
            if(shape->second.Reference==reference)shape=_MeshShapes.erase(shape);else ++shape;
        }
        found=_ResourceRevisions.erase(found);changed=true;
    }
    if(changed)++_ResourceGeneration;
}
IComponent *RuntimePhysicsSystem::ResolveComponent(uint64_t objectID, uint64_t componentID) const {
    auto *state = _Runtime.Find(objectID);
    if (!_Runtime.IsInScene(state) || !state->IsActiveInHierarchy())
        return nullptr;
    for (const auto &entry : state->Components)
        if (!entry.PendingRemove && entry.Initialized && !entry.InitializationFailed &&
            entry.Pointer->GetComponentInstanceID() == componentID)
            return entry.Pointer;
    return nullptr;
}
bool RuntimePhysicsSystem::AppendCollider(BodyRequest &request, PhysicsColliderState &state,
                                          std::string &error) {
    IColliderComponent *collider = state.ColliderInterface();
    const auto identity =
        ColliderIdentity{collider->GetGameObject()->GetScene()->GetInstanceID(),
                         collider->GetGameObject()->GetInstanceID(), collider->GetComponentInstanceID()};
    const auto key = std::make_tuple(identity.SceneID, identity.ObjectID, identity.ComponentID);
    auto found = _ColliderKeys.find(key);
    if (found == _ColliderKeys.end())
        found = _ColliderKeys.emplace(key, ++_NextColliderKey).first;
    _Identities[found->second] = identity;
    GPhysics::Shape shape;
    shape.Collider = found->second;
    shape.Trigger = state.Settings.IsTrigger;
    shape.Layer = collider->GetGameObject()->GetLayer();
    shape.Mask = _Runtime.GetProjectSettings().CollisionMatrix[shape.Layer];
    if (!state.Settings.Material.Guid.empty()) {
        if (!_Provider) {
            error = "Collider physics material requires a prepared resource provider";
            return false;
        }
        const auto materialKey =
            std::make_pair(state.Settings.Material.Guid, state.Settings.Material.LocalID);
        auto material = _Materials.find(materialKey);
        if (material == _Materials.end()) {
            PhysicsMaterialData data;
            if (!_Provider->LoadPhysicsMaterial(state.Settings.Material, data, error))
                return false;
            if (!std::isfinite(data.Friction) || data.Friction < 0 || !std::isfinite(data.Restitution) ||
                data.Restitution < 0 || data.Restitution > 1 ||
                data.FrictionCombine > PhysicsMaterialCombine::Maximum ||
                data.RestitutionCombine > PhysicsMaterialCombine::Maximum) {
                error = "Invalid prepared physics material";
                return false;
            }
            material = _Materials.emplace(materialKey, data).first;
            _ResourceRevisions[materialKey]=_Provider->GetResourceRevision(state.Settings.Material);
        }
        shape.Surface = {material->second.Friction, material->second.Restitution,
                         static_cast<GPhysics::CombineMode>(material->second.FrictionCombine),
                         static_cast<GPhysics::CombineMode>(material->second.RestitutionCombine)};
    }
    const Mat4 colliderWorld =
        collider->GetGameObject()->GetTransform()->GetWorldMatrix() *
        RuntimeMath::ComposeTRS(state.Settings.Center, state.Settings.Rotation, {1, 1, 1});
    const Mat4 rigidPose =
        RuntimeMath::ComposeTRS(RuntimeVector(request.Description.Transform.Position),
                                RuntimeRotation(request.Description.Transform.Rotation), {1, 1, 1});
    Mat4 inverse;
    if (!RuntimeMath::TryInverseAffine(rigidPose, inverse)) {
        error = "Cannot invert physics body transform";
        return false;
    }
    if (auto *mesh=dynamic_cast<MeshColliderComponent*>(&state)) {
        if(!AppendMeshCollider(request,*mesh,shape,inverse*colliderWorld,error))return false;
        if(shape.Mesh)request.Description.Shapes.push_back(shape);
        return true;
    }
    RuntimePhysicsMath::ColliderFrame frame;
    const auto primitiveWorld=dynamic_cast<SphereColliderComponent*>(&state)?
        collider->GetGameObject()->GetTransform()->GetWorldMatrix()*RuntimeMath::ComposeTRS(state.Settings.Center,Quaternion{},{1,1,1}):colliderWorld;
    if(!RuntimePhysicsMath::TryGetColliderFrame(inverse*primitiveWorld,frame)) {
        error="Primitive collider transform requires a non-zero orthogonal TRS (no shear)";return false;
    }
    const Vector3 scale=RuntimePhysicsMath::AbsoluteScale(frame.Scale);
    shape.LocalPose={PhysicsVector(frame.Position),PhysicsRotation(frame.Rotation)};
    if (auto *box = dynamic_cast<BoxColliderComponent *>(&state)) {
        shape.Type = GPhysics::ShapeType::Box;
        shape.HalfExtents = {box->Shape.Size.X * scale.X * 0.5f, box->Shape.Size.Y * scale.Y * 0.5f,
                             box->Shape.Size.Z * scale.Z * 0.5f};
    } else if (auto *sphere = dynamic_cast<SphereColliderComponent *>(&state)) {
        shape.Type = GPhysics::ShapeType::Sphere;
        shape.Radius = sphere->Shape.Radius * RuntimePhysicsMath::SphereScale(scale);
    } else if (auto *capsule = dynamic_cast<CapsuleColliderComponent *>(&state)) {
        shape.Type = GPhysics::ShapeType::Capsule;
        shape.Radius = capsule->Shape.Radius * RuntimePhysicsMath::CapsuleRadiusScale(scale,capsule->Shape.Axis);
        const float height=std::max(capsule->Shape.Height*RuntimePhysicsMath::CapsuleHeightScale(scale,capsule->Shape.Axis),shape.Radius*2);
        shape.HalfHeight = height*.5f-shape.Radius;
        const float half = std::sqrt(0.5f);
        Quaternion axis = capsule->Shape.Axis == PhysicsAxis::X   ? Quaternion{0, 0, -half, half}
                          : capsule->Shape.Axis == PhysicsAxis::Z ? Quaternion{half, 0, 0, half}
                                                                  : Quaternion{};
        Mat4 axisMatrix = RuntimeMath::ComposeTRS({}, axis, {1, 1, 1});
        const Mat4 rotationMatrix=RuntimeMath::ComposeTRS({},frame.Rotation,{1,1,1})*axisMatrix;
        RuntimePhysicsMath::ColliderFrame axisFrame;
        if(!RuntimePhysicsMath::TryGetColliderFrame(rotationMatrix,axisFrame)) {
            error="Capsule collider axis transform is invalid";return false;
        }
        shape.LocalPose.Rotation=PhysicsRotation(axisFrame.Rotation);
    } else {
        error = "Unknown collider implementation";
        return false;
    }
    request.Description.Shapes.push_back(shape);
    return true;
}
bool RuntimePhysicsSystem::BuildRequests(IScene *scene, std::map<uint64_t, BodyRequest> &requests,
                                         std::string &error) {
    std::vector<IGameObject *> objects;
    for (std::size_t i = 0; i < scene->GetChildCount(); ++i)
        objects.push_back(scene->GetChild(i));
    for (std::size_t i = 0; i < objects.size(); ++i) {
        auto *object = objects[i];
        auto *node = _Runtime.Find(object);
        if (!_Runtime.IsInScene(node) || !node->IsActiveInHierarchy())
            continue;
        for (std::size_t child = 0; child < object->GetChildCount(); ++child)
            objects.push_back(object->GetChild(child));
    }
    const auto createRequest = [&](IGameObject *object, RigidbodyComponent *rigid) -> BodyRequest * {
        const auto key = object->GetInstanceID();
        auto found = requests.find(key);
        if (found != requests.end())
            return &found->second;
        BodyRequest request;
        request.Object = object;
        request.Rigidbody = rigid;
        request.World = object->GetTransform()->GetWorldMatrix();
        request.Description.Key = key;
        if (!PhysicsBodyPose(request.World, request.Description.Transform, request.WorldScale, !rigid, error))
            return nullptr;
        if (rigid) {
            const auto &settings = rigid->Settings;
            request.Description.Motion = settings.MotionType == PhysicsMotionType::Dynamic
                                             ? GPhysics::MotionType::Dynamic
                                             : GPhysics::MotionType::Kinematic;
            request.Description.Mass = settings.Mass;
            request.Description.GravityFactor = settings.GravityFactor;
            request.Description.LinearDamping = settings.LinearDamping;
            request.Description.AngularDamping = settings.AngularDamping;
            request.Description.AllowedDOFs = static_cast<uint8_t>((~settings.Constraints) & 63u);
            request.Description.Continuous = settings.Continuous;
            request.Description.LinearVelocity = PhysicsVector(rigid->LinearVelocity);
            request.Description.AngularVelocity = PhysicsVector(rigid->AngularVelocity);
        }
        auto [entry, inserted] = requests.emplace(key, std::move(request));
        return &entry->second;
    };
    for (IGameObject *object : objects) {
        auto *node = _Runtime.Find(object);
        if (!_Runtime.IsInScene(node) || !node->IsActiveInHierarchy())
            continue;
        for (const auto &entry : node->Components) {
            if (entry.PendingRemove || entry.InitializationFailed)
                continue;
            if (auto *rigid = dynamic_cast<RigidbodyComponent *>(entry.Pointer);
                rigid && rigid->Settings.Enabled)
                if (!createRequest(object, rigid))
                    return false;
        }
    }
    for (IGameObject *object : objects) {
        auto *node = _Runtime.Find(object);
        if (!_Runtime.IsInScene(node) || !node->IsActiveInHierarchy())
            continue;
        for (const auto &entry : node->Components) {
            if (entry.PendingRemove || entry.InitializationFailed)
                continue;
            auto *collider = dynamic_cast<PhysicsColliderState *>(entry.Pointer);
            if (!collider || !collider->Settings.Enabled)
                continue;
            RigidbodyComponent *attached = nullptr;
            bool boundary = false;
            for (IGameObject *parent = object; parent && !boundary; parent = parent->Parent()) {
                auto *parentState = _Runtime.Find(parent);
                if (!parentState)
                    break;
                for (const auto &candidate : parentState->Components)
                    if (!candidate.PendingRemove && !candidate.InitializationFailed)
                        if (auto *rigid = dynamic_cast<RigidbodyComponent *>(candidate.Pointer)) {
                            boundary = true;
                            if (rigid->Settings.Enabled)
                                attached = rigid;
                            break;
                        }
            }
            BodyRequest *request = createRequest(attached ? attached->GetGameObject() : object, attached);
            if (!request || !AppendCollider(*request, *collider, error))
                return false;
        }
    }
    return true;
}
bool RuntimePhysicsSystem::Synchronize(IScene *scene, SceneWorld &world, float delta, std::string &error) {
    SyncResources();
    const auto revision=static_cast<Scene*>(scene)->GetRenderRevision();
    const auto& matrix=_Runtime.GetProjectSettings().CollisionMatrix;
    world.UnchangedStatic=world.SnapshotKnown&&world.StaticOnly&&world.SceneRevision==revision&&
        world.Provider==_Provider&&world.ResourceGeneration==_ResourceGeneration&&world.CollisionMatrix==matrix;
    if(world.UnchangedStatic)return true;
    world.SnapshotKnown=false;
    std::map<uint64_t, BodyRequest> requests;
    if (!BuildRequests(scene, requests, error))
        return false;
    for (auto it = world.Bodies.begin(); it != world.Bodies.end();)
        if (!requests.count(it->first)) {
            world.Backend.RemoveBody(it->first);
            if (auto *object = _Runtime.QueryGameObject(it->first))
                if (auto *transform = dynamic_cast<TransformComponent *>(object->GetTransform()))
                    transform->ClearPhysicsPose();
            it = world.Bodies.erase(it);
        } else
            ++it;
    for (auto &[key, request] : requests) {
        auto found = world.Bodies.find(key);
        auto *transform = dynamic_cast<TransformComponent *>(request.Object->GetTransform());
        const auto componentID = request.Rigidbody ? request.Rigidbody->GetComponentInstanceID() : 0;
        const bool rebuild = found == world.Bodies.end() || found->second.ComponentID != componentID ||
                             !PhysicsBodyEqual(found->second.Description, request.Description);
        if (rebuild) {
            GPhysics::BodyState old;
            if (found != world.Bodies.end() && componentID == found->second.ComponentID &&
                world.Backend.GetBodyState(key, old)) {
                request.Description.LinearVelocity = old.LinearVelocity;
                request.Description.AngularVelocity = old.AngularVelocity;
            }
            if (!world.Backend.PutBody(request.Description, error))
                return false;
            BodyRecord record;
            record.Description = request.Description;
            for(auto &shape:record.Description.Shapes)shape.Mesh=nullptr;
            record.ComponentID = componentID;
            record.WorldScale = request.WorldScale;
            record.LastWorld = request.World;
            record.TransformRevision = transform->GetAuthoringRevision();
            if (request.Rigidbody) {
                record.ConfigurationRevision = request.Rigidbody->Revision;
                record.VelocityRevision = request.Rigidbody->VelocityRevision;
            }
            world.Bodies[key] = record;
            found = world.Bodies.find(key);
        }
        BodyRecord &record = found->second;
        if (request.Description.Motion == GPhysics::MotionType::Static) {
            if (!PhysicsNear(record.LastWorld, request.World))
                world.Backend.SetPose(key, request.Description.Transform, false, delta);
            record.LastWorld = request.World;
            continue;
        }
        auto &rigid = *request.Rigidbody;
        if (rigid.VelocityRevision != record.VelocityRevision) {
            world.Backend.SetVelocity(key, PhysicsVector(rigid.LinearVelocity),
                                      PhysicsVector(rigid.AngularVelocity));
            record.VelocityRevision = rigid.VelocityRevision;
        }
        if (rigid.WakeRequested) {
            GPhysics::BodyState body;
            if (world.Backend.GetBodyState(key, body))
                world.Backend.SetVelocity(key, body.LinearVelocity, body.AngularVelocity);
            rigid.WakeRequested = false;
        }
        if (rigid.HasTeleport) {
            world.Backend.SetPose(
                key, {PhysicsVector(rigid.TargetPosition), PhysicsRotation(rigid.TargetRotation)}, false,
                delta);
            if (rigid.ClearTeleportVelocity)
                world.Backend.SetVelocity(key, {}, {});
            rigid.HasTeleport = false;
        } else if (rigid.HasKinematicTarget) {
            world.Backend.SetPose(
                key, {PhysicsVector(rigid.TargetPosition), PhysicsRotation(rigid.TargetRotation)}, true,
                delta);
            rigid.HasKinematicTarget = false;
            record.KinematicMoved = true;
        } else if (!PhysicsNear(record.LastWorld, request.World) ||
                   record.TransformRevision != transform->GetAuthoringRevision()) {
            world.Backend.SetPose(key, request.Description.Transform,
                                  rigid.Settings.MotionType == PhysicsMotionType::Kinematic, delta);
            record.KinematicMoved = rigid.Settings.MotionType == PhysicsMotionType::Kinematic;
        } else if (record.KinematicMoved) {
            world.Backend.SetVelocity(key, {}, {});
            record.KinematicMoved = false;
        }
        record.TransformRevision = transform->GetAuthoringRevision();
        if (delta > 0) {
            world.Backend.AddForce(key, PhysicsVector(rigid.Force), false);
            world.Backend.AddForce(key, PhysicsVector(rigid.Impulse), true);
            rigid.Force = {};
            rigid.Impulse = {};
        }
    }
    world.StaticOnly=std::none_of(world.Bodies.begin(),world.Bodies.end(),
        [](const auto& entry){return entry.second.ComponentID!=0;});
    world.SceneRevision=static_cast<Scene*>(scene)->GetRenderRevision();
    world.Provider=_Provider;world.ResourceGeneration=_ResourceGeneration;world.CollisionMatrix=matrix;
    world.SnapshotKnown=true;
    return true;
}
} // namespace GameSproutRuntime

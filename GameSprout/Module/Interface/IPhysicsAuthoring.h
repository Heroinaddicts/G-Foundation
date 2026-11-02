#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimePhysicsData.h"
#include "ResourceReference.h"

struct ColliderEditTarget {
    std::uint64_t SceneID=0,ObjectID=0,ComponentID=0;
    bool operator==(const ColliderEditTarget&) const = default;
};
enum class ColliderShapeKind : std::uint8_t { Box,Sphere,Capsule,Mesh };
struct ColliderEditState {
    ColliderShapeKind Kind=ColliderShapeKind::Box;
    GameSproutRuntime::ColliderSettings Settings;
    GameSproutRuntime::BoxColliderShape Box;
    GameSproutRuntime::SphereColliderShape Sphere;
    GameSproutRuntime::CapsuleColliderShape Capsule;
    GameSproutRuntime::MeshColliderShape Mesh;
};

// Editor 物理作者服务：源资源、校验与文件事务，不负责模拟或界面。
class IPhysicsAuthoring : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IPhysicsAuthoring() = default;
    // Read is permitted during a host frame; mutation requires the owner-thread
    // idle point. Identity values never keep a Runtime node alive.
    virtual bool ReadCollider(const ColliderEditTarget&,ColliderEditState&,std::string& error) const {error="物理作者服务不支持碰撞体查询。";return false;}
    virtual bool CanEditCollider(const ColliderEditTarget&,std::string& error) const {error="物理作者服务不支持碰撞体编辑。";return false;}
    virtual bool FitCollider(const ColliderEditTarget&,std::string& error) {error="物理作者服务不支持碰撞体拟合。";return false;}
    virtual bool BeginColliderEdit(const ColliderEditTarget&,std::string& error) {error="物理作者服务不支持连续碰撞体编辑。";return false;}
    virtual bool PreviewColliderEdit(const ColliderEditState&,std::string& error) {error="没有碰撞体编辑事务。";return false;}
    virtual bool CommitColliderEdit(std::string& error) {error="没有碰撞体编辑事务。";return false;}
    virtual bool CancelColliderEdit(std::string& error) {error.clear();return true;}
    virtual bool HasColliderEdit() const {return false;}
    virtual bool CreatePhysicsMaterialAsset(const std::string& directory,const std::string& name,
        std::string& path,std::string& error) = 0;
    // LocalID=0 denotes the standalone source, including before a successful import.
    virtual bool LoadPhysicsMaterialSource(const GameSproutRuntime::ResourceReference& reference,
        GameSproutRuntime::PhysicsMaterialData& data,std::string& sourceSnapshot,std::string& error) const = 0;
    // expectedSourceSnapshot 拒绝覆盖外部修改。参数属于源资产，不属于 Meta 导入设置。
    virtual bool SavePhysicsMaterialSource(const GameSproutRuntime::ResourceReference& reference,
        const GameSproutRuntime::PhysicsMaterialData& data,const std::string& expectedSourceSnapshot,
        std::string& error) = 0;
};

#pragma once
#include "Header.h"
class IAssetsManager;
class ILibraryManager;
class IProject;
class ISceneManager;
class IObjectEditing;
class QThread;

class PhysicsAuthoring final : public IPhysicsAuthoring {
public:
    ~PhysicsAuthoring() override;
    bool ReadCollider(const ColliderEditTarget&,ColliderEditState&,std::string&) const override;
    bool CanEditCollider(const ColliderEditTarget&,std::string&) const override;
    bool FitCollider(const ColliderEditTarget&,std::string&) override;
    bool BeginColliderEdit(const ColliderEditTarget&,std::string&) override;
    bool PreviewColliderEdit(const ColliderEditState&,std::string&) override;
    bool CommitColliderEdit(std::string&) override;
    bool CancelColliderEdit(std::string&) override;
    bool HasColliderEdit() const override;
    bool CreatePhysicsMaterialAsset(const std::string&,const std::string&,std::string&,std::string&) override;
    bool LoadPhysicsMaterialSource(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::PhysicsMaterialData&,
        std::string&,std::string&) const override;
    bool SavePhysicsMaterialSource(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::PhysicsMaterialData&,
        const std::string&,std::string&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool MaterialPath(const GameSproutRuntime::ResourceReference&,std::string&,std::string&) const;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    IProject* _Project=nullptr;
    ISceneManager* _Scenes=nullptr;
    IObjectEditing* _Objects=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
    GameSproutRuntime::IColliderComponent* ResolveCollider(const ColliderEditTarget&) const;
    struct ColliderEdit {
        bool Active=false;
        ColliderEditTarget Target;
        std::uint64_t FileID=0;
        ColliderEditState Before,Last;
    } _ColliderEdit;
};

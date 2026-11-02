#pragma once
#include "Header.h"
class IAssetsManager;
class ILibraryManager;
class IProject;

class PhysicsAuthoring final : public IPhysicsAuthoring {
public:
    ~PhysicsAuthoring() override;
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
};

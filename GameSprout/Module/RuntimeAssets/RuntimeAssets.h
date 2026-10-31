#pragma once
#include "IRuntimeAssets.h"
class IAssetsManager;
class ILibraryManager;
class IProject;
class LibraryRuntimeResources;
class RuntimeAssets final : public IRuntimeAssets {
public:
    ~RuntimeAssets() override;
    GameSproutRuntime::IRuntimeResourceProvider* GetResourceProvider() const override;
    bool CreateAnimatorControllerAsset(const std::string&,const std::string&,
        const GameSproutRuntime::ResourceReference&,std::string&,std::string&) override;
    bool LoadAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,
        GameSproutRuntime::AnimatorControllerData&,std::string&,std::string&) const override;
    bool SaveAnimatorControllerSource(const GameSproutRuntime::ResourceReference&,
        const GameSproutRuntime::AnimatorControllerData&,const std::string&,std::string&) override;
    bool CreateAvatarMaskSource(const std::string&,const std::string&,
        const GameSproutRuntime::ResourceReference&,std::string&,std::string&) override;
    bool LoadAvatarMaskSource(const GameSproutRuntime::ResourceReference&,
        GameSproutRuntime::AvatarMaskData&,std::string&,std::string&) const override;
    bool SaveAvatarMaskSource(const GameSproutRuntime::ResourceReference&,
        const GameSproutRuntime::AvatarMaskData&,const std::string&,std::string&) override;
    bool LoadModelRigSource(const GameSproutRuntime::ResourceReference&,ModelRigImportSettings&,GameSproutRuntime::SkeletonData&,std::string&,std::string&) const override;
    bool SaveModelRigSource(const GameSproutRuntime::ResourceReference&,const ModelRigImportSettings&,const std::string&,std::string&) override;
    bool SuggestHumanoidMapping(const GameSproutRuntime::SkeletonData&,std::vector<GameSproutRuntime::HumanBoneMapping>&,std::string&) const override;
private:
    bool MaskPath(const GameSproutRuntime::ResourceReference&,std::string&,std::string&) const;
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return _Resources!=nullptr;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    bool ControllerPath(const GameSproutRuntime::ResourceReference&,std::string&,std::string&) const;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    IProject* _Project=nullptr;
    LibraryRuntimeResources* _Resources=nullptr;
};

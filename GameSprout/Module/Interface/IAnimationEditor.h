#pragma once
#include "GameSproutEditorApi.h"
#include "RuntimeResourceApi.h"
#include "HumanoidImportSettings.h"

// Editor 动画资产作者服务；负责源文件/导入设置，不拥有 Runtime、预览或界面。
class IAnimationEditor : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~IAnimationEditor() = default;
    // 创建当前目录下的 .controller，不覆盖现有源文件或 Meta。
    virtual bool CreateAnimatorControllerAsset(const std::string& directory, const std::string& name,
        const GameSproutRuntime::ResourceReference& model, std::string& path, std::string& error) = 0;
    // 编辑源快照与运行产物分开；expectedJson 用于拒绝覆盖外部修改。
    virtual bool LoadAnimatorControllerSource(const GameSproutRuntime::ResourceReference& reference,
        GameSproutRuntime::AnimatorControllerData& data, std::string& json, std::string& error) const = 0;
    virtual bool SaveAnimatorControllerSource(const GameSproutRuntime::ResourceReference& reference,
        const GameSproutRuntime::AnimatorControllerData& data, const std::string& expectedJson,
        std::string& error) = 0;
    // Mask source editing belongs to the Module. Accept a Model or Skeleton resource
    // when creating; the stored source always identifies its concrete Skeleton.
    virtual bool CreateAvatarMaskSource(const std::string& directory,const std::string& name,
        const GameSproutRuntime::ResourceReference& skeletonOrModel,std::string& path,std::string& error) = 0;
    virtual bool LoadAvatarMaskSource(const GameSproutRuntime::ResourceReference& reference,
        GameSproutRuntime::AvatarMaskData& data,std::string& sourceSnapshot,std::string& error) const = 0;
    virtual bool SaveAvatarMaskSource(const GameSproutRuntime::ResourceReference& reference,
        const GameSproutRuntime::AvatarMaskData& data,const std::string& expectedSourceSnapshot,std::string& error) = 0;
    // Avatar is a Model subresource. Rig edits update the owning Model metadata.
    virtual bool LoadModelRigSource(const GameSproutRuntime::ResourceReference& reference,
        ModelRigImportSettings& settings,GameSproutRuntime::SkeletonData& skeleton,
        std::string& sourceSnapshot,std::string& error) const = 0;
    virtual bool SaveModelRigSource(const GameSproutRuntime::ResourceReference& reference,
        const ModelRigImportSettings& settings,const std::string& expectedSourceSnapshot,std::string& error) = 0;
    virtual bool SuggestHumanoidMapping(const GameSproutRuntime::SkeletonData& skeleton,
        std::vector<GameSproutRuntime::HumanBoneMapping>& mappings,std::string& error) const = 0;
};

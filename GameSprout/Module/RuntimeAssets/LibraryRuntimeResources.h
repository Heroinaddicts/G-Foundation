#pragma once
#include "RuntimeResourceApi.h"
#include "IAssetsManager.h"

class ILibraryManager;
namespace Json { class Value; }
class QByteArray;

// 编辑器适配层：查询 ArtifactDB 的布局描述，读取 Library 已预处理的产物。
// Runtime 本身不依赖编辑器模块、Qt、源文件格式或数据库。
class LibraryRuntimeResources final : public GameSproutRuntime::IRuntimeResourceProvider, public IAssetListener {
public:
    LibraryRuntimeResources(IAssetsManager* assets, ILibraryManager* library);
    ~LibraryRuntimeResources() override;
    std::uint64_t GetRevision() const override { return _Revision; }
    bool LoadMesh(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::MeshRenderData&, std::string&) override;
    bool LoadTexture(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::TextureRenderData&, std::string&) override;
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::MaterialRenderData&, std::string&) override;
    bool LoadModel(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::ModelData&, std::string&) override;
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::SkeletonData&, std::string&) override;
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AnimationClipData&, std::string&) override;
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AnimatorControllerData&, std::string&) override;
    bool LoadHumanoidAvatar(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::HumanoidAvatarData&, std::string&) override;
    bool LoadAvatarMask(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AvatarMaskData&, std::string&) override;
    void OnAssetCreate(const AssetEvent&) override { ++_Revision; }
    void OnAssetModify(const AssetEvent&) override { ++_Revision; }
    void OnAssetRemove(const AssetEvent&) override { ++_Revision; }
private:
    bool Read(const GameSproutRuntime::ResourceReference&, eAssetType, Json::Value&, QByteArray&, std::string&);
    IAssetsManager* _Assets = nullptr;
    ILibraryManager* _Library = nullptr;
    std::uint64_t _Revision = 1;
    bool _Subscribed = false;
};

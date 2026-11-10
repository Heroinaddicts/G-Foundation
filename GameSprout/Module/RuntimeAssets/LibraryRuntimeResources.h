#pragma once
#include "RuntimeResourceApi.h"
#include "IAssetsManager.h"
#include <map>
#include <set>
#include <functional>

class ILibraryManager;
namespace Json { class Value; }
class QByteArray;
class QFile;

// 编辑器适配层：查询 ArtifactDB 的布局描述，读取 Library 已预处理的产物。
// Runtime 本身不依赖编辑器模块、Qt、源文件格式或数据库。
class LibraryRuntimeResources final : public GameSproutRuntime::IRuntimeResourceProvider, public IAssetListener {
public:
    LibraryRuntimeResources(IAssetsManager* assets, ILibraryManager* library);
    ~LibraryRuntimeResources() override;
    std::uint64_t GetRevision() const override { return _Revision; }
    std::uint64_t GetResourceRevision(const GameSproutRuntime::ResourceReference&) const override;
    std::function<void()> ResourcesChanged;
    bool LoadMesh(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::MeshRenderData&, std::string&) override;
    bool LoadTexture(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::TextureRenderData&, std::string&) override;
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::MaterialRenderData&, std::string&) override;
    bool LoadShader(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::ShaderRenderData&, std::string&) override;
    bool LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::PhysicsMaterialData&,std::string&) override;
    bool LoadModel(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::ModelData&, std::string&) override;
    bool LoadPrefab(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::PrefabData&, std::string&) override;
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::SkeletonData&, std::string&) override;
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AnimationClipData&, std::string&) override;
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AnimatorControllerData&, std::string&) override;
    bool LoadHumanoidAvatar(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::HumanoidAvatarData&, std::string&) override;
    bool LoadAvatarMask(const GameSproutRuntime::ResourceReference&, GameSproutRuntime::AvatarMaskData&, std::string&) override;
    bool LoadMeshCollision(const GameSproutRuntime::ResourceReference&, bool convex,
                           std::vector<std::uint8_t>&, std::string&) override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
private:
    struct ResourceKey {
        std::string Guid;
        std::uint64_t LocalID=0;
        bool operator<(const ResourceKey& other) const {
            return Guid<other.Guid||(Guid==other.Guid&&LocalID<other.LocalID);
        }
        bool operator==(const ResourceKey&) const = default;
    };
    struct ResourceSignature {
        eAssetType Type=eAssetType::Unknown;
        std::uint32_t DataVersion=0;
        std::uint64_t DataSize=0;
        std::string Hash,DescriptorJson;
        bool operator==(const ResourceSignature&) const = default;
    };
    struct ResourceState {
        ResourceSignature Signature;
        std::uint64_t Revision=1;
        bool Present=false;
        bool ReadFailed=false;
        std::set<ResourceKey> Dependencies;
    };
    struct ObservedGuid {
        std::map<std::uint64_t,ResourceState> Resources;
        std::string PublicationGeneration;
    };
    // Only GUIDs actually requested by Runtime are observed. Missing entries remain
    // as tombstones so restoring the same identity invalidates a cached failure.
    ObservedGuid* ObserveGuid(const std::string&) const;
    ResourceState* ObserveResource(const GameSproutRuntime::ResourceReference&) const;
    std::map<std::uint64_t,ResourceSignature> ReadSignatures(const Guid&,std::string&) const;
    void RefreshPublishedResources(const AssetEvent&);
    void InvalidateResources(const std::set<ResourceKey>&);
    void ResetDependencies(const GameSproutRuntime::ResourceReference&);
    void AddDependency(const GameSproutRuntime::ResourceReference&,const GameSproutRuntime::ResourceReference&);
    bool Read(const GameSproutRuntime::ResourceReference&, eAssetType, Json::Value&, QByteArray&, std::string&);
    bool OpenArtifact(const GameSproutRuntime::ResourceReference&, eAssetType, Json::Value&, QFile&,
                      ResourceState*&, std::string&);
    IAssetsManager* _Assets = nullptr;
    ILibraryManager* _Library = nullptr;
    std::uint64_t _Revision = 1;
    bool _Subscribed = false;
    mutable std::map<std::string,ObservedGuid> _ObservedGuids;
    std::map<ResourceKey,std::set<ResourceKey>> _Dependents;
};

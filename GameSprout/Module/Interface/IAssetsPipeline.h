#ifndef __IAssetsPipeline_h__
#define __IAssetsPipeline_h__

#include "GameSproutApi.h"
#include "IGuidCreator.h"
#include <functional>
#include <string>
#include <vector>

class IAssetMeta;

// An imported resource descriptor, not a GPU allocation. Library owns these objects.
class IAsset {
public:
    virtual ~IAsset() {}
    virtual const Guid & GetGuid() const = 0;
    virtual uint64_t GetLocalID() const = 0;
    virtual eAssetType GetAssetType() const = 0;
    virtual const std::string & GetName() const = 0;
    virtual const std::string & GetArtifactPath() const = 0;
};

struct AssetArtifact {
    uint64_t LocalID = 0;
    eAssetType Type = eAssetType::Unknown;
    std::string Name;
    std::string SourceKey;
    std::string Path; // Relative to this manifest's artifact directory.
    std::string Hash;
};

struct AssetDependency {
    std::string Path; // Relative to project Assets.
    std::string Hash;
};

struct AssetImportManifest {
    std::string GuidString;
    std::string SourcePath;
    std::string SourceHash;
    std::string SettingsHash;
    std::string Target = "Portable";
    uint32_t ImporterVersion = 1;
    std::string Directory; // Empty for flat artifacts; legacy manifests may name a directory.
    std::string Generation; // Publication identity, independent of the stable filename.
    std::string SubAssetsJson = "[]";
    std::vector<AssetArtifact> Assets;
    std::vector<AssetDependency> Dependencies;
};

struct AssetImportResult {
    bool Success = false;
    bool Cached = false;
    bool Superseded = false;
    bool Retry = false;
    std::string Error;
    AssetImportManifest Manifest;
};

class IAssetsPipeline : public GameSprout::IGameSproutModule {
public:
    virtual ~IAssetsPipeline() {}
    // Results are borrowed for the callback only. Callbacks run on Foundation::Update's thread.
    // Meta is snapshotted at submission; no worker retains its address.
    typedef std::function<void(const AssetImportResult &)> fImportCallback;
    // 主线程调用。内部在提交 TaskApi 前 Loading，完成回调执行后 Unloading。
    // 提交失败、任务取消和模块释放也由内部配对；调用方不要为本次导入重复调用。
    virtual void ImportAsset(IAssetMeta * meta, const fImportCallback & callback, bool force = false) = 0;
    // Prevent an obsolete task from committing after source deletion/rename/change.
    virtual void Invalidate(const Guid & guid) = 0;
};

#endif //__IAssetsPipeline_h__

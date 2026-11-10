#pragma once
#include "IAssetsPipeline.h"
#include "ITaskApi.h"
#include "../Shared/AssetArtifactIO.h"
#include "../Shared/AssetFileStamp.h"
#include "AssetFileFingerprintCache.h"
#include <QDateTime>
#include <map>

class IShaderCompiler;
// 完整导入与启动快速校验必须使用相同的算法版本。
constexpr uint32_t AssetImportAlgorithmVersion(eAssetImporterType type) {
    switch (type) {
    case eAssetImporterType::Model: return 8; // Unity LH/Y-up/+Z-forward geometry and animation.
    case eAssetImporterType::AnimationClip: return 6; // Current Unity world coordinate contract.
    case eAssetImporterType::AnimatorController: return 5;
    case eAssetImporterType::AvatarMask: return 4;
    case eAssetImporterType::Scene: return 5; // SceneDocument2, Unity world coordinates.
    case eAssetImporterType::Prefab: return 4; // Embedded SceneDocument2, Unity world coordinates.
    case eAssetImporterType::Mesh: return 4; // Current native GSMESH02 Unity source -> GPU bytes.
    case eAssetImporterType::Shader: return 4; // Complete graphics Pass programs and reflected material bindings.
    case eAssetImporterType::Material: return 4; // Current engine rendering options, independent of Shader reflection.
    case eAssetImporterType::PhysicsMaterial:
    case eAssetImporterType::ComputeShader: return 3;
    default: return 2;
    }
}

struct AssetImportPreparedResource {
    eAssetType Type=eAssetType::Unknown;
    std::string Path,DescriptorJson,Hash;
    uint64_t Size=0;uint32_t Version=0;
};
struct AssetImportRequest {
    AssetFileFingerprintCache * Fingerprints = nullptr; // 借用模块缓存，宿主在线程 join 后销毁模块。
    IShaderCompiler * Compiler = nullptr; // Initialized editor module; kept alive until all tasks finish.
    std::string GuidString;
    std::string AssetsRoot;
    std::string LibraryRoot;
    std::string SourcePath;
    std::string MetaJson;
    std::map<std::string, std::string> SourcePaths;
    std::map<std::string, AssetImportPreparedResource> PreparedResources; // copied on main thread; never retain database objects.
    eAssetImporterType Importer = eAssetImporterType::Unknown;
    std::set<std::string> AnimationDependencies,DeclaredAnimationDependencies,FailedAnimationDependencies;
    uint64_t Revision = 0;
    std::string AnimationValidationJson; // current v2 prepared-input cache contract, captured on main thread.
    bool HasPrevious = false;
    bool Force = false;
    AssetImportManifest Previous;
};

struct AssetImportJob {
    explicit AssetImportJob(AssetImportRequest request) : Request(std::move(request)) {
        if (!Request.Fingerprints) { _LocalFingerprints.Open(); Request.Fingerprints = &_LocalFingerprints; }
    }
    ~AssetImportJob();
    AssetImportRequest Request;
    AssetImportResult Result;
    Json::Value Settings;
    Json::Value Identities{Json::arrayValue};
    QByteArray SourceBytes;
    std::string Stage;
    qint64 SourceSize = 0;
    QDateTime SourceModified;
    std::map<std::string, uint64_t> IDs;
    std::set<uint64_t> UsedIDs;
    std::set<std::string> ProducedKeys;
    // 原生文件版本与内容读取绑定；完成回调和快速启动都检查同一版本。
    std::map<std::string, AssetFileStamp> ReadVersions;
    std::map<std::string, AssetFileStamp> SearchDirectoryVersions;
    // 与 Manifest.Dependencies 同步清空；重复引用复用已验证身份，不重复解析 Meta。
    std::map<std::string, std::size_t> DependencyIndices;
    bool ValidationAvailable = true;
    std::string MismatchedPreparedResource; // internal key; main-thread completion resolves replacement vs stable corruption.
    Foundation::ITaskContext * Context = nullptr;

    bool Cancelled();
    bool Fail(const std::string & error);
    uint64_t ID(eAssetType type, const std::string & key, const std::string & name);
    bool Emit(eAssetType type, const std::string & key, const std::string & name,
        Json::Value metadata, const QByteArray & payload = {});
    bool AddDependency(const std::string & relativePath, QByteArray * bytes = nullptr, bool recheck = false);
    std::string TrackGuidReference(const std::string& guid);
    bool ReadFile(const std::string & path, QByteArray * bytes, std::string & hash);
private:
    AssetFileFingerprintCache _LocalFingerprints; // 独立导入调用也可用，不持有堆对象。
};

bool RunAssetImport(AssetImportJob & job, Foundation::ITaskContext & context);
std::string AssetGuidSourcePath(const AssetImportRequest&,const std::string& guid);
bool AssetGuidSourcesCurrent(const AssetImportRequest&,const AssetImportManifest&);
class IAssetsManager;
bool AssetGuidSourcesCurrent(const AssetImportManifest&,const IAssetsManager*,const std::string& assetsRoot);
bool ModelTextureSearchesCurrent(const AssetImportRequest& request,const AssetImportManifest& manifest,
    std::map<std::string, AssetFileStamp> * observations = nullptr, bool * observationsAvailable = nullptr);
bool ImportTexture(AssetImportJob & job);
bool ImportTextureBytes(AssetImportJob & job, QByteArray bytes, const std::string & key,
    const std::string & name, const Json::Value & settings);
bool ImportModel(AssetImportJob & job);
bool ImportNativeMesh(AssetImportJob & job);
bool ImportMedia(AssetImportJob & job);
bool ImportFont(AssetImportJob & job);
bool ImportDocument(AssetImportJob & job);
bool ResolveScenePrefabInstances(AssetImportJob&,Json::Value& document);
bool ImportShader(AssetImportJob & job);

bool ImportAnimationDocument(AssetImportJob&,eAssetType,const Json::Value&);

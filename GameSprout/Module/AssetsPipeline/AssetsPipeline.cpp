#include "AssetsPipeline.h"
#include "AssetImportJob.h"
#include "IAssetsManager.h"
#include "IShaderCompiler.h"
#include <QDateTime>
#include <exception>

namespace {
struct LoadingCompletion {
    IIDEWindow * Window;
    ~LoadingCompletion() { Window->Unloading(); }
};
}


void AssetsPipeline::BeforeInitialize(GameSproutApi * api) {
    g_Self = this;
    _Tasks = api->_Foundation->TaskApi();
    GetGameSproutModule(api->_EditorApi, _Project, Project);
    GetGameSproutModule(api->_EditorApi, _Assets, AssetsManager);
    GetGameSproutModule(api->_EditorApi, _Library, LibraryManager);
    GetGameSproutModule(api->_EditorApi, _ShaderCompiler, ShaderCompiler);
    GetGameSproutView(api->_EditorApi, _IDE, IDEWindow);
}
bool AssetsPipeline::Initialize(GameSproutApi * api) { g_GameSproutApi = api; return _Tasks && _Project && _Assets && _Library && _ShaderCompiler && _IDE; }
bool AssetsPipeline::Launch(GameSproutApi *) { _Owner = _Tasks->RegisterTaskOwner(); return _Owner != Foundation::InvalidTaskOwnerId; }
void AssetsPipeline::LaunchFinished(GameSproutApi *) {}
void AssetsPipeline::Update(GameSproutApi *) {}
void AssetsPipeline::Release(GameSproutApi *) {
    if (_Tasks && _Owner != Foundation::InvalidTaskOwnerId) _Tasks->UnregisterTaskOwner(_Owner);
    _Owner = Foundation::InvalidTaskOwnerId;
    for (const auto & task : _Running) { (void)task; if (_IDE) _IDE->Unloading(); }
    _Running.clear();
    _Revisions.clear();
}

void AssetsPipeline::Invalidate(const Guid & guid) {
    const std::string key = guid.GetGuidString();
    ++_Revisions[key];
    for (const auto & task : _Running) if (task.second == key) _Tasks->CancelTask(task.first);
}

void AssetsPipeline::ImportAsset(IAssetMeta * meta, const fImportCallback & callback, bool force) {
    AssetImportRequest request;
    if (!meta || !_Tasks || _Owner == Foundation::InvalidTaskOwnerId || !meta->SerializeMeta(request.MetaJson)) {
        AssetImportResult failed; failed.Error = "Invalid Meta or unavailable pipeline";
        if (callback) callback(failed);
        return;
    }
    request.GuidString = meta->_Guid.GetGuidString();
    request.Force = force;
    request.Importer = meta->_ImporterType;
    request.Compiler = _ShaderCompiler;
    request.SourcePath = meta->GetPath();
    // Only native resource documents contain GUID references needing this lookup snapshot.
    switch (request.Importer) {
    case eAssetImporterType::Mesh: case eAssetImporterType::Material: case eAssetImporterType::Scene:
    case eAssetImporterType::AnimationClip: case eAssetImporterType::AnimatorController:
    case eAssetImporterType::AnimatorOverrideController: case eAssetImporterType::AvatarMask:
    case eAssetImporterType::AudioMixer: case eAssetImporterType::PhysicsMaterial:
    case eAssetImporterType::PhysicsMaterial2D: case eAssetImporterType::RenderTexture:
    case eAssetImporterType::Cubemap: case eAssetImporterType::SpriteAtlas:
    case eAssetImporterType::TerrainData: case eAssetImporterType::TerrainLayer:
    case eAssetImporterType::ShaderVariantCollection: case eAssetImporterType::LightingSettings:
    case eAssetImporterType::LightmapParameters:
        for (const IAssetMeta * item : _Assets->GetAssetMetas()) request.SourcePaths.emplace(item->_Guid.GetGuidString(), item->GetPath());
        break;
    default: break;
    }
    request.AssetsRoot = QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets").toStdString();
    request.LibraryRoot = _Library->GetLibraryRoot();
    request.Revision = ++_Revisions[request.GuidString];
    request.HasPrevious = _Library->GetManifest(meta->_Guid, request.Previous);
    Foundation::TaskSubmitOptions options;
    options._OwnerId = _Owner;
    _IDE->Loading("资源导入", request.SourcePath);
    bool loadingNotTransferred = true;
    try {
        const auto submission = _Tasks->SubmitTask(options,
            [request](Int64 & code, Foundation::ITaskContext & context) -> bool {
                // Allocate in the worker: a task cancelled before execution owns no raw allocation.
                AssetImportJob * job = new AssetImportJob(request);
                if (!context.SetOwned(job)) { delete job; code = -1; return false; }
                try { const bool success = RunAssetImport(*job, context); code = success ? 0 : -1; return success; }
                catch (const std::exception & error) { job->Fail(error.what()); code = -1; return false; }
                catch (...) { job->Fail("Unexpected import exception"); code = -1; return false; }
            },
            [this, request, callback](const Foundation::TaskCompletion & completion, bool succeeded,
                                     const Foundation::ITaskContext & context) {
                LoadingCompletion balance{_IDE};
                _Running.erase(completion._TaskId);
                AssetImportJob * job = context.Get<AssetImportJob>();
                AssetImportResult result;
                if (job) result = job->Result;
                result.Success = succeeded && job && result.Success;
                const auto revision = _Revisions.find(request.GuidString);
                if (revision == _Revisions.end() || revision->second != request.Revision) {
                    result.Success = false; result.Superseded = true; result.Retry = false; result.Error = "Import superseded";
                }
                if (result.Success) {
                    for (const auto & stamp : job->DependencyStamps) {
                        const QFileInfo dependency(QString::fromStdString(stamp.first));
                        if (!dependency.isFile() || dependency.size() != stamp.second.first || dependency.lastModified() != stamp.second.second) {
                            result.Success = false; result.Superseded = true; result.Retry = true; result.Error = "Dependency changed before commit"; break;
                        }
                    }
                }
                if (result.Success) {
                    QFileInfo current(QString::fromStdString(request.AssetsRoot + "/" + request.SourcePath));
                    if (!current.isFile() || current.size() != job->SourceSize || current.lastModified() != job->SourceModified) {
                        result.Success = false; result.Superseded = true; result.Retry = true; result.Error = "Source changed before commit";
                    } else if (!result.Cached) {
                        result.Success = _Library->CommitImport(job->Stage, result.Manifest, result.Error);
                        if (result.Success) job->Stage.clear();
                    }
                }
                if (!result.Success && result.Error.empty()) result.Error = "Import cancelled";
                // Completion runs on Foundation::Update, including Library/LMDB and UI operations.
                if (callback) callback(result);
            });
        if (submission.Accepted()) {
            loadingNotTransferred = false;
            _Running.emplace(submission._TaskId, request.GuidString);
        }
        else {
            _IDE->Unloading();
            loadingNotTransferred = false;
            AssetImportResult failed; failed.Error = "TaskApi rejected resource import";
            if (callback) callback(failed);
        }
    } catch (...) { if (loadingNotTransferred) _IDE->Unloading(); throw; }
}

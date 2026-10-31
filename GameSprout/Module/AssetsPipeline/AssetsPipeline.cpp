#include "AssetsPipeline.h"
#include "AssetImportJob.h"
#include "AnimationResourceSnapshot.h"
#include "IAssetsManager.h"
#include "IShaderCompiler.h"
#include <QDateTime>
#include <exception>

struct AssetsPipeline::ImportCompletion {
    AssetsPipeline * Owner;
    uint64_t Batch;
    const std::string & Path;
    bool Loading;
    bool Active = true;
    ~ImportCompletion() {
        if (!Active) return;
        Owner->FinishImportProgress(Batch, Path);
        if (Loading) Owner->_IDE->Unloading();
        Owner->ResumeAnimationImports();
    }
};


void AssetsPipeline::BeforeInitialize(GameSproutModuleApi * api) {
    g_Self = this;
    _Tasks = api->_Foundation->TaskApi();
    GetGameSproutModule(api->_EditorApi, _Project, Project);
    GetGameSproutModule(api->_EditorApi, _Assets, AssetsManager);
    GetGameSproutModule(api->_EditorApi, _Library, LibraryManager);
    GetGameSproutModule(api->_EditorApi, _ShaderCompiler, ShaderCompiler);
    GetGameSproutView(api->_EditorApi, _IDE, IDEWindow);
    if (_IDE) _IDE->BindTaskApi(_Tasks);
}
bool AssetsPipeline::Initialize(GameSproutModuleApi * api) { g_GameSproutModuleApi = api; return _Tasks && _Project && _Assets && _Library && _ShaderCompiler && _IDE; }
bool AssetsPipeline::Launch(GameSproutModuleApi *) {
    _Releasing=false;
    _Owner = _Tasks->RegisterTaskOwner();
    if (_Owner == Foundation::InvalidTaskOwnerId) return false;
    _Fingerprints.Open();
    return true;
}
void AssetsPipeline::LaunchFinished(GameSproutModuleApi *) {}
void AssetsPipeline::Update(GameSproutModuleApi *) {}
void AssetsPipeline::Release(GameSproutModuleApi *) {
    _Releasing=true;
    if (_Tasks && _Owner != Foundation::InvalidTaskOwnerId) _Tasks->UnregisterTaskOwner(_Owner);
    _Owner = Foundation::InvalidTaskOwnerId;
    _Fingerprints.Close(); // 对象继续存活，晚到的 worker 不能发布缓存条目。
    if (_IDE) ClearImportProgress();
    for (const auto & task : _Running) { (void)task; if (_IDE) _IDE->Unloading(); }
    _Running.clear();
    for(const auto& entry:_DeferredAnimationImports)if(_IDE)_IDE->Unloading();
    _DeferredAnimationImports.clear();
    _DeclaredAnimationDependencies.clear();
    _Revisions.clear();
}

void AssetsPipeline::Invalidate(const Guid & guid) {
    const std::string key = guid.GetGuidString();
    ++_Revisions[key];
    const auto deferred=_DeferredAnimationImports.find(key);
    if(deferred!=_DeferredAnimationImports.end()) {
        auto entry=std::move(deferred->second);_DeferredAnimationImports.erase(deferred);
        ImportCompletion completion{this,entry.Batch,entry.Path,true};
        AssetImportResult result;result.Superseded=true;result.Error="Import superseded";
        if(entry.Callback)entry.Callback(result);
    }
    for (const auto & task : _Running) if (task.second.GuidString == key) _Tasks->CancelTask(task.first);
}

void AssetsPipeline::ImportAsset(IAssetMeta* meta,const fImportCallback& callback,bool force) {
    ImportAssetInternal(meta,callback,force,0,false);
}
void AssetsPipeline::ImportAssetInternal(IAssetMeta * meta, const fImportCallback & callback, bool force,uint64_t existingBatch,bool resumed) {
    AssetImportRequest request;
    const std::string path = meta ? meta->GetPath() : "无效资源";
    const uint64_t batch = resumed?existingBatch:(_IDE ? RegisterImportProgress(path) : 0);
    ImportCompletion admission{this, batch, path, resumed};
    if (!meta || !_Tasks || _Owner == Foundation::InvalidTaskOwnerId || !meta->SerializeMeta(request.MetaJson)) {
        AssetImportResult failed; failed.Error = "Invalid Meta or unavailable pipeline";
        if (callback) callback(failed);
        return;
    }
    request.Fingerprints = &_Fingerprints;
    request.GuidString = meta->_Guid.GetGuidString();
    request.Force = force;
    request.Importer = meta->_ImporterType;
    request.Compiler = _ShaderCompiler;
    request.SourcePath = meta->GetPath();
    // 只复制模型显式映射的材质身份；普通模型不遍历整份资源列表。
    if (request.Importer == eAssetImporterType::Model) {
        Json::Value snapshot;
        if (AssetArtifactIO::Parse(request.MetaJson, snapshot)) {
            const Json::Value& remaps = snapshot["ImportSettings"]["MaterialRemaps"];
            if (remaps.isObject()) for (const auto& reference : remaps) {
                if (!reference["Guid"].isString()) continue;
                const Guid guid(reference["Guid"].asString());
                const IAssetMeta* target = guid.IsValid() ? _Assets->QueryAsset(guid) : nullptr;
                if (target) request.SourcePaths.emplace(guid.GetGuidString(), target->GetPath());
            }
        }
    }
    // Native resource documents can contain arbitrary GUID references.
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
    CaptureAnimationResources(request,_Assets,_Library);
    if(request.Importer==eAssetImporterType::AnimatorController||request.Importer==eAssetImporterType::AvatarMask||request.Importer==eAssetImporterType::AnimationClip) {
        auto& edges=_DeclaredAnimationDependencies[request.GuidString];edges.clear();
        for(const auto& dependency:request.DeclaredAnimationDependencies) {
            const auto source=request.SourcePaths.find(dependency);
            edges[dependency]=source==request.SourcePaths.end()?"":source->second;
        }
    }
    request.Revision = ++_Revisions[request.GuidString];
    request.HasPrevious = _Library->GetManifest(meta->_Guid, request.Previous);
    if (request.HasPrevious) {
        RestoreFileFingerprints(request.Previous, request.AssetsRoot);
        if(request.Importer==eAssetImporterType::AnimatorController||request.Importer==eAssetImporterType::AvatarMask||request.Importer==eAssetImporterType::AnimationClip)
            _Library->GetImportValidation(meta->_Guid,request.AnimationValidationJson);
    }
    Foundation::TaskSubmitOptions options;
    options._OwnerId = _Owner;
    if(!resumed)_IDE->Loading("资源导入", request.SourcePath);
    admission.Loading = true;
    const auto previousDeferred=_DeferredAnimationImports.find(request.GuidString);
    if(previousDeferred!=_DeferredAnimationImports.end()) {
        auto previous=std::move(previousDeferred->second);_DeferredAnimationImports.erase(previousDeferred);
        {
            ImportCompletion retirement{this,previous.Batch,previous.Path,true};
            AssetImportResult superseded;superseded.Superseded=true;superseded.Error="Import superseded";
            if(previous.Callback)previous.Callback(superseded);
        }
        // A callback can submit a still newer request for the same GUID.
        if(_Revisions[request.GuidString]!=request.Revision) {
            AssetImportResult superseded;superseded.Superseded=true;superseded.Error="Import superseded";
            if(callback)callback(superseded);return;
        }
    }
    for(const auto& dependency:request.AnimationDependencies) {
        const auto* target=_Assets->QueryAsset(Guid(dependency));
        if(target&&target->GetImportState()==eAssetImportState::Pending) {
            _DeferredAnimationImports[request.GuidString]={request.GuidString,path,callback,force,batch,request.AnimationDependencies};
            admission.Active=false;return;
        }
    }
    const auto submission = _Tasks->SubmitTask(options,
        [request](Int64 & code, Foundation::ITaskContext & context) -> bool {
            // Allocate in the worker: a task cancelled before execution owns no raw allocation.
            AssetImportJob * job = new AssetImportJob(request);
            if (!context.SetOwned(job)) { delete job; code = -1; return false; }
            try { const bool success = RunAssetImport(*job, context); code = success ? 0 : -1; return success; }
            catch (const std::exception & error) { job->Fail(error.what()); code = -1; return false; }
            catch (...) { job->Fail("Unexpected import exception"); code = -1; return false; }
        },
        [this, request, callback, batch](const Foundation::TaskCompletion & completion, bool succeeded,
                                 const Foundation::ITaskContext & context) {
            // Release 可能已经取消并结算本项，迟到回调不能再次推进进度或解除遮罩。
            const auto running = _Running.find(completion._TaskId);
            if (running == _Running.end()) return;
            _Running.erase(running);
            ImportCompletion balance{this, batch, request.SourcePath, true};
            AssetImportJob * job = context.Get<AssetImportJob>();
            AssetImportResult result;
            if (job) result = job->Result;
            result.Success = succeeded && job && result.Success;
            if(job)ResolveAnimationSnapshotMismatch(*job,_Assets,_Library,result);
            const auto revision = _Revisions.find(request.GuidString);
            if (revision == _Revisions.end() || revision->second != request.Revision) {
                result.Success = false; result.Superseded = true; result.Retry = false; result.Error = "Import superseded";
            }
            if (result.Success) {
                for (const auto & observed : job->ReadVersions) {
                    AssetFileStamp current;
                    if (!ReadAssetFileStamp(observed.first, current) || current != observed.second) {
                        result.Success = false; result.Superseded = true; result.Retry = true;
                        result.Error = "File version changed before commit"; break;
                    }
                }
            }
            if(result.Success&&!ModelTextureSearchesCurrent(request,result.Manifest,&job->SearchDirectoryVersions,&job->ValidationAvailable)) {
                result.Success=false;result.Superseded=true;result.Retry=true;result.Error="Texture lookup changed before commit";
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
            // 回调先完成 SubAssets/Meta 保存，再记录最终身份。工作线程的版本仍须匹配。
            const auto currentRevision = _Revisions.find(request.GuidString);
            if (result.Success && currentRevision != _Revisions.end() && currentRevision->second == request.Revision)
                SaveImportValidation(*job, result);
            ResumeAnimationImports();
        });
    if (submission.Accepted()) {
        try { _Running.emplace(submission._TaskId, RunningImport{request.GuidString, batch}); }
        catch (...) { _Tasks->CancelTask(submission._TaskId); throw; }
        admission.Active = false;
    } else {
        AssetImportResult failed; failed.Error = "TaskApi rejected resource import";
        if (callback) callback(failed);
    }
}

void AssetsPipeline::ResumeAnimationImports() {
    if(_Releasing||_ResumingAnimationImports)return;
    _ResumingAnimationImports=true;
    struct Reset { bool& Value;~Reset(){Value=false;} } reset{_ResumingAnimationImports};
    // A resumed import may fail synchronously and unblock another item. Drain in
    // this same main-thread completion, without recursion or Update polling.
    for(;;) {
        std::vector<DeferredAnimationImport> ready;
        for(auto iterator=_DeferredAnimationImports.begin();iterator!=_DeferredAnimationImports.end();) {
            bool pending=false;
            for(const auto& dependency:iterator->second.Dependencies) {
                const auto* meta=_Assets->QueryAsset(Guid(dependency));
                if(meta&&meta->GetImportState()==eAssetImportState::Pending){pending=true;break;}
            }
            if(pending){++iterator;continue;}
            ready.push_back(std::move(iterator->second));iterator=_DeferredAnimationImports.erase(iterator);
        }
        if(ready.empty())break;
        for(auto& entry:ready)ImportAssetInternal(_Assets->QueryAsset(Guid(entry.GuidString)),entry.Callback,entry.Force,entry.Batch,true);
    }
}

std::vector<std::string> AssetsPipeline::FindSourceDependents(const std::string& relativePath) const {
    std::vector<std::string> result;
    for(const auto& owner:_DeclaredAnimationDependencies) {
        if(!_Assets->QueryAsset(Guid(owner.first)))continue;
        for(const auto& dependency:owner.second) {
            const auto* meta=_Assets->QueryAsset(Guid(dependency.first));
            if(dependency.second==relativePath||(meta&&meta->GetPath()==relativePath)) {
                result.push_back(owner.first);break;
            }
        }
    }
    return result;
}

#include "SceneManager.h"
#include "SceneDocument.h"
#include "SceneSaveSnapshot.h"
#include "IIDEWindow.h"
#include "../Shared/AssetArtifactIO.h"
#include "../Shared/AssetFileStamp.h"
#include "../Shared/AssetSourceIO.h"
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <chrono>
#include <limits>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace {
bool ParseSceneBytes(const QByteArray& bytes,Json::Value& document) {
    Json::CharReaderBuilder builder;
    builder["rejectDupKeys"]=true;
    builder["failIfExtra"]=true;
    struct ReaderScope {
        Json::CharReader* Reader;
        ~ReaderScope() { delete Reader; }
    } parser{builder.newCharReader()};
    return parser.Reader->parse(bytes.constData(),bytes.constData()+bytes.size(),&document,nullptr);
}

std::string HashSceneBytes(const QByteArray& bytes) {
#if defined(__APPLE__)
    if(static_cast<std::uint64_t>(bytes.size())<=std::numeric_limits<CC_LONG>::max()) {
        unsigned char digest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256(bytes.constData(),static_cast<CC_LONG>(bytes.size()),digest);
        return QByteArray::fromRawData(reinterpret_cast<const char*>(digest),sizeof(digest)).toHex().toStdString();
    }
#endif
    return AssetArtifactIO::Hash(bytes);
}

bool DiskMatches(const QString& path,const std::string& expected,std::string& error) {
    QFile source(path);
    if(expected.empty() || !source.open(QIODevice::ReadOnly)) {
        error="磁盘场景已消失或无法核对，未覆盖。请重新打开场景或另存为新文件。";return false;
    }
    const QByteArray bytes=source.readAll();
    if(source.error()!=QFileDevice::NoError || HashSceneBytes(bytes)!=expected) {
        error="场景文件已被外部修改，未覆盖。请重新打开场景或另存为新文件。";return false;
    }
    return true;
}
bool Resolve(IProject* project,const std::string& relative,bool directory,QString& output,std::string& error,bool allowMissing=false) {
    if(!project || project->GetProjectRoot().empty()) {error="项目尚未打开。";return false;}
    QString path=QString::fromStdString(relative);
    path.replace('\\','/');
    if(QDir::isAbsolutePath(path) || path.contains(':') || path.contains(QChar(0))) {error="需要相对于 Assets 的有效路径。";return false;}
    path=QDir::cleanPath(path);
    if(path==".." || path.startsWith("../")) {error="路径不能超出 Assets。";return false;}
    if(path==".") path.clear();
    const QString assets=QDir(QString::fromStdString(project->GetProjectRoot())).filePath("Assets");
    const QFileInfo root(assets);
    if(!root.isDir() || root.isSymLink()) {error="项目 Assets 目录不存在或为符号链接。";return false;}
    QString current=root.canonicalFilePath();
    const QString canonicalRoot=current;
    for(const QString& part:path.split('/',Qt::SkipEmptyParts)) {
        current=QDir(current).filePath(part);
        if(QFileInfo(current).isSymLink()) {error="场景路径不能经过符号链接。";return false;}
    }
    const QFileInfo target(current);
    if(!target.exists() && allowMissing && !directory) {
        const QFileInfo parent(target.absolutePath());
        if(!parent.isDir() || parent.isSymLink()) {error="场景所在目录不存在。";return false;}
        const QString canonicalParent=parent.canonicalFilePath();
        if(canonicalParent!=canonicalRoot && !canonicalParent.startsWith(canonicalRoot+'/')) {error="路径不能超出 Assets。";return false;}
        output=QDir(canonicalParent).filePath(target.fileName());return true;
    }
    if(!target.exists() || (directory?!target.isDir():!target.isFile())) {error=directory?"目标文件夹不存在。":"场景文件不存在。";return false;}
    const QString canonical=target.canonicalFilePath();
    if(canonical!=canonicalRoot && !canonical.startsWith(canonicalRoot+'/')) {error="路径不能超出 Assets。";return false;}
    output=canonical;return true;
}

struct SceneReadResult {
    SceneDocument::PreparedScene Prepared;
    Json::Value Annotations{Json::objectValue};
    AssetFileStamp Stamp;
    std::string Hash,Error;
};

struct SceneBaselineResult {
    SceneDirtyState State;
};

bool ReadSceneFile(const QString& source,SceneReadResult& result) {
    const auto path=source.toStdString();
    AssetFileStamp before;
    if(!ReadAssetFileStamp(path,before)||!before.Exists||before.Directory) {
        result.Error="场景文件已移动或无法读取。";return false;
    }
    QFile file(source);
    if(!file.open(QIODevice::ReadOnly)) {result.Error=file.errorString().toStdString();return false;}
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError) {result.Error=file.errorString().toStdString();return false;}
    if(!ReadAssetFileStamp(path,result.Stamp)||result.Stamp!=before||result.Stamp.Size!=static_cast<std::uint64_t>(bytes.size())) {
        result.Error="读取期间场景文件已被修改，请重新打开。";return false;
    }
    Json::Value document;
    if(!ParseSceneBytes(bytes,document)) {result.Error="场景 JSON 格式无效。";return false;}
    if(document.isObject()&&document.isMember("Annotations"))result.Annotations=document["Annotations"];
    result.Hash=HashSceneBytes(bytes);
    return SceneDocument::Prepare(std::move(document),result.Prepared,result.Error);
}
}

struct SceneManager::OpenRequest {
    std::string Path;
    QString Source;
    fSceneOpenCompleted Completed;
    UInt64 Task=Foundation::InvalidTaskId;
    SceneReadResult SourceData;
    SceneDocument::SceneAssembly* Assembly=nullptr;
    std::uint64_t CandidateID=0;
    SceneDocument::ObjectIDs IDs;
    SceneDirtyState Baseline;
    std::vector<std::uint64_t> Pending;
    std::vector<Json::Value> Rows;
    bool Read=false;
};

bool SceneManager::CreateSceneAsset(const std::string& directory,const std::string& name,std::string& outPath,std::string& error) {
    error.clear();outPath.clear();
    QString folder;
    if(!Resolve(_Project,directory,true,folder,error)) return false;
    QString stem=QString::fromStdString(name).trimmed();
    if(stem.endsWith(".scene",Qt::CaseInsensitive)) stem.chop(6);
    const QRegularExpression illegal(R"([<>:"/\\|?*\x00-\x1f])");
    const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))",QRegularExpression::CaseInsensitiveOption);
    if(stem.isEmpty() || stem=="." || stem==".." || stem.endsWith('.') || stem.endsWith(' ') ||
       illegal.match(stem).hasMatch() || reserved.match(stem).hasMatch()) {
        error="场景名称无效，请使用普通文件名，不要包含路径或特殊字符。";return false;
    }
    const QString filename=QDir(folder).filePath(stem+".scene");
    const QByteArray bytes=QByteArray::fromStdString(AssetSourceIO::JsonText(SceneDocument::CreateDefault(stem.toStdString())));
    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
        error=QFileInfo::exists(filename)?"同名场景已存在，不能覆盖。":file.errorString().toStdString();return false;
    }
    if(file.write(bytes)!=bytes.size() || !file.flush()) {
        error=file.errorString().toStdString();file.close();file.remove();return false;
    }
    file.close();
    const QString assetsPath=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
    const QDir assets(QFileInfo(assetsPath).canonicalFilePath());
    outPath=assets.relativeFilePath(filename).toStdString();
    return true;
}

bool SceneManager::CanOpenScene(std::string& error) const {
    error.clear();
    if(IsDocumentSessionActive()){error="请先关闭当前临时文档，再打开场景。";return false;}
    if(IsPlaySessionActive()){error="请先停止运行，再打开其他编辑场景。";return false;}
    if(!Runtime() || _Switching || _ExecutingEdit || _Notifying || _TransformEdit.Active || _PropertyPreview) {error="请先结束场景编辑，场景服务当前不能加载其他场景。";return false;}
    if(Runtime()->IsExecuting()||Runtime()->IsScriptExecutionEnabled()) {error="请先结束 Runtime 回调或运行，再打开场景。";return false;}
    return true;
}

bool SceneManager::ResolveSceneFile(const std::string& path,QString& source,std::string& relative,std::string& error) const {
    if(!Resolve(_Project,path,false,source,error)) return false;
    if(QFileInfo(source).suffix().compare("scene",Qt::CaseInsensitive)!=0) {error="请选择 .scene 场景文件。";return false;}
    const QString assetsPath=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
    relative=QDir(QFileInfo(assetsPath).canonicalFilePath()).relativeFilePath(source).toStdString();
    return true;
}

bool SceneManager::OpenScene(const std::string& path,std::string& error) {
    QString source;
    std::string relative;
    if(!CanOpenScene(error)||!ResolveSceneFile(path,source,relative,error))return false;
    _Switching=true;
    GameSproutRuntime::IScene* candidate=nullptr;
    SceneDocument::ObjectIDs ids;
    bool success=false;
    try {
        SceneReadResult result;
        success=ReadSceneFile(source,result);
        if(success)success=SceneDocument::Instantiate(result.Prepared,*Runtime(),candidate,ids,error);
        else error=std::move(result.Error);
        if(success) {
            Activate(candidate,std::move(relative),std::move(ids),std::move(result.Hash),std::move(result.Annotations));
        }
    } catch(const std::exception& exception) {error=exception.what();success=false;}
    catch(...) {error="场景加载失败。";success=false;}
    if(!success && candidate && candidate!=GetActiveScene()) Runtime()->DestroyScene(candidate);
    _Switching=false;
    return success;
}

bool SceneManager::OpenSceneAsync(const std::string& path,fSceneOpenCompleted completed,std::string& error) {
    QString source;
    std::string relative;
    if(!CanOpenScene(error)||!ResolveSceneFile(path,source,relative,error))return false;
    if(_TaskOwner==Foundation::InvalidTaskOwnerId) {error="场景后台任务服务尚未就绪。";return false;}
    auto* request=new OpenRequest{std::move(relative),source,std::move(completed)};
    _OpenRequest=request;
    _Switching=true;
    _IDE->Loading("打开场景",request->Path);
    Foundation::TaskSubmitResult submission;
    try {submission=_Tasks->SubmitTask({_TaskOwner},[source](Int64& code,Foundation::ITaskContext& context) {
        auto* result=new SceneReadResult;
        if(!context.SetOwned(result)) {delete result;return false;}
        if(context.IsCancellationRequested()||!ReadSceneFile(source,*result))return false;
        code=0;return true;
    },[this](const Foundation::TaskCompletion& completion,bool success,const Foundation::ITaskContext& context) {
        auto* request=_OpenRequest;
        if(!request||request->Task!=completion._TaskId)return;
        request->Task=Foundation::InvalidTaskId;
        auto* result=context.Get<SceneReadResult>();
        if(!success||!result) {
            FinishOpenScene(false,result&&!result->Error.empty()?std::move(result->Error):"无法读取或校验场景。");return;
        }
        request->SourceData=std::move(*result);
        request->Read=true;
    });} catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="无法提交场景打开任务。";}
    if(!submission.Accepted()) {
        request->Completed={};
        if(error.empty())error="无法提交场景打开任务。";
        FinishOpenScene(false,error);return false;
    }
    request->Task=submission._TaskId;
    return true;
}

void SceneManager::AdvanceOpenScene() {
    auto& request=*_OpenRequest;
    if(!request.Read||request.Task!=Foundation::InvalidTaskId)return;
    using Clock=std::chrono::steady_clock;
    const auto deadline=Clock::now()+std::chrono::milliseconds(6);
    std::string error;
    try {
        if(!request.CandidateID) {
            if(!request.Assembly) {
                _IDE->SetLoadingInfo("打开场景","正在创建节点和组件…");
                request.Assembly=SceneDocument::BeginAssembly(request.SourceData.Prepared,*Runtime(),error);
                if(!request.Assembly) {FinishOpenScene(false,std::move(error));return;}
            }
            GameSproutRuntime::IScene* candidate=nullptr;
            const auto status=SceneDocument::AdvanceAssembly(*request.Assembly,deadline,candidate,request.IDs,error);
            if(status==SceneDocument::AssemblyStatus::Failed) {FinishOpenScene(false,std::move(error));return;}
            if(status==SceneDocument::AssemblyStatus::Pending)return;
            SceneDocument::DestroyAssembly(request.Assembly);request.Assembly=nullptr;
            request.CandidateID=candidate->GetInstanceID();
            candidate->SetSimulationEnabled(false);
            _IDE->SetLoadingInfo("打开场景","正在准备保存基准…");
            return;
        }
        auto* candidate=dynamic_cast<GameSproutRuntime::IScene*>(Runtime()->QueryGameObject(request.CandidateID));
        if(!candidate) {FinishOpenScene(false,"候选场景已失效。");return;}
        if(request.Baseline.Known) {
            std::string relative;
            QString source;
            AssetFileStamp current;
            if(!ResolveSceneFile(request.Path,source,relative,error)||source!=request.Source||
               !ReadAssetFileStamp(source.toStdString(),current)||current!=request.SourceData.Stamp) {
                FinishOpenScene(false,"打开期间场景文件已被移动或修改，请重新打开。");return;
            }
            if(candidate->GetAuthoringRevision()==request.Baseline.Revision) {
                candidate->SetSimulationEnabled(true);
                Activate(candidate,std::move(request.Path),std::move(request.IDs),std::move(request.SourceData.Hash),
                         std::move(request.SourceData.Annotations),std::move(request.Baseline));
                request.CandidateID=0;
                FinishOpenScene(true,{});return;
            }
            request.Baseline={};
        }
        if(request.Baseline.SceneID==0||request.Baseline.Revision!=candidate->GetAuthoringRevision()) {
            auto& baseline=request.Baseline;
            baseline={};request.Pending.clear();request.Rows.clear();
            baseline.SceneID=request.CandidateID;
            baseline.Revision=baseline.NotifiedRevision=candidate->GetAuthoringRevision();
            if(!SceneDocument::SerializeProperties(*candidate,request.IDs,baseline.BaselineRoot,error)) {
                FinishOpenScene(false,std::move(error));return;
            }
            if(!request.SourceData.Annotations.empty())baseline.BaselineRoot["Annotations"]=request.SourceData.Annotations;
            baseline.CurrentRoot=baseline.BaselineRoot;
            request.Rows.reserve(request.IDs.RuntimeToFile.size());
            for(auto index=candidate->GetChildCount();index>0;--index)
                request.Pending.push_back(candidate->GetChild(index-1)->GetInstanceID());
        }
        while(!request.Pending.empty()&&Clock::now()<deadline) {
            auto* node=Runtime()->QueryGameObject(request.Pending.back());request.Pending.pop_back();
            Json::Value properties;
            if(!node||node->GetScene()!=candidate) {FinishOpenScene(false,"候选场景节点已失效。");return;}
            if(!SceneDocument::SerializeProperties(*node,request.IDs,properties,error)) {
                FinishOpenScene(false,std::move(error));return;
            }
            request.Rows.push_back(std::move(properties));
            for(auto index=node->GetChildCount();index>0;--index)request.Pending.push_back(node->GetChild(index-1)->GetInstanceID());
        }
        if(!request.Pending.empty())return;
        SubmitOpenSceneBaseline();
    } catch(const std::exception& exception) {FinishOpenScene(false,exception.what());}
      catch(...) {FinishOpenScene(false,"场景加载失败。");}
}

void SceneManager::SubmitOpenSceneBaseline() {
    auto& request=*_OpenRequest;
    const auto submission=_Tasks->SubmitTask({_TaskOwner},
        [rows=std::move(request.Rows),state=std::move(request.Baseline),source=request.SourceData.Prepared.ReleaseDocument()]
        (Int64& code,Foundation::ITaskContext& context) mutable {
            source=Json::Value{};
            auto* result=new SceneBaselineResult;
            if(!context.SetOwned(result)) {delete result;return false;}
            state.BaselineOrder.reserve(rows.size());
            for(const auto& row:rows) {
                if(context.IsCancellationRequested())return false;
                const auto id=row["ID"].asUInt64();
                state.BaselineOrder.push_back(id);
                state.BaselineNodes.emplace(id,SceneSaveSnapshot::Row(row));
                for(const auto& component:row["Components"])
                    if(component["Type"]=="GameSproutRuntime.Animator"||component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer") {
                        state.AnimationOwners.insert(id);break;
                    }
            }
            rows.clear();
            state.CurrentOrder=state.BaselineOrder;state.Known=true;
            result->State=std::move(state);code=0;return true;
        },[this](const Foundation::TaskCompletion& completion,bool success,const Foundation::ITaskContext& context) {
            auto* request=_OpenRequest;
            if(!request||request->Task!=completion._TaskId)return;
            request->Task=Foundation::InvalidTaskId;
            auto* result=context.Get<SceneBaselineResult>();
            if(!success||!result) {FinishOpenScene(false,"无法准备场景保存基准。");return;}
            request->Baseline=std::move(result->State);
        });
    if(submission.Accepted())request.Task=submission._TaskId;
    else FinishOpenScene(false,"无法提交场景保存基准任务。");
}

void SceneManager::FinishOpenScene(bool success,std::string error) {
    auto* request=_OpenRequest;
    if(!request)return;
    _OpenRequest=nullptr;
    SceneDocument::DestroyAssembly(request->Assembly);
    if(request->CandidateID&&request->CandidateID!=_ActiveID)
        if(auto* candidate=dynamic_cast<GameSproutRuntime::IScene*>(Runtime()->QueryGameObject(request->CandidateID)))Runtime()->DestroyScene(candidate);
    auto completed=std::move(request->Completed);
    delete request;
    _Switching=false;
    if(!success&&_ActiveID)_StatePending=true;
    _IDE->Unloading();
    if(completed)try {completed(success,error);} catch(...) {qWarning("Scene open callback threw an exception");}
}

void SceneManager::CancelOpenScene() {
    if(_TaskOwner!=Foundation::InvalidTaskOwnerId)_Tasks->UnregisterTaskOwner(_TaskOwner);
    _TaskOwner=Foundation::InvalidTaskOwnerId;
    if(_OpenRequest) {
        _OpenRequest->Completed={};
        FinishOpenScene(false,{});
    }
}

bool SceneManager::SaveScene(std::string& error) {
    if(IsDocumentSessionActive()){error="临时文档应由其所属模块保存，不能写为.scene。";return false;}
    if (!Ready(error)) return false;
    if (_ActivePath.empty()) {error="尚未指定场景路径，请先另存为 .scene 文件。";return false;}
    return SaveTo(_ActivePath,false,error);
}
bool SceneManager::SaveSceneAs(const std::string& relativePath,std::string& error) {
    if(IsDocumentSessionActive()){error="临时文档不能另存为.scene。";return false;}
    if (!Ready(error)) return false;
    return SaveTo(relativePath,true,error);
}
bool SceneManager::SaveTo(const std::string& path,bool allowNew,std::string& error) {
    QString target;
    if (!Resolve(_Project,path,false,target,error,true)) return false;
    const QFileInfo destination(target);
    if (destination.suffix().compare("scene",Qt::CaseInsensitive)!=0) {error="场景文件必须使用 .scene 后缀。";return false;}
    const QRegularExpression illegal(R"([<>:"/\\|?*\x00-\x1f])");
    const QRegularExpression reserved(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))",QRegularExpression::CaseInsensitiveOption);
    if (destination.completeBaseName().isEmpty() || illegal.match(destination.fileName()).hasMatch() ||
        reserved.match(destination.fileName()).hasMatch() || destination.fileName().endsWith(' ') || destination.fileName().endsWith('.')) {
        error="场景文件名无效。";return false;
    }
    const QString assetsPath=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets");
    const QDir assets(QFileInfo(assetsPath).canonicalFilePath());
    const std::string normalized=assets.relativeFilePath(target).toStdString();
    const bool exists=destination.exists();
    const bool currentPath=normalized==_ActivePath;
    if (!allowNew && !exists) {error="磁盘场景已消失，未重新创建。请重新打开场景或另存为新文件。";return false;}
    if (exists && normalized!=_ActivePath) {error="目标场景已存在，不能静默覆盖另一个场景。";return false;}
    if (currentPath && !DiskMatches(target,_DiskHash,error)) return false;
    if(!SynchronizeDirtyState(error))return false;
    // Still check external edits on a clean scene, but do not replace its file
    // or generate a watcher/import job when the authored values did not change.
    if(currentPath&&!DirtyFromSnapshot()) {
        const bool notify=_DirtyState.NotifiedDirty||_DirtyState.NotifiedRevision!=_DirtyState.Revision;
        _Unsaved=false;_StatePending=false;
        _DirtyState.NotifiedDirty=false;_DirtyState.NotifiedRevision=_DirtyState.Revision;
        if(notify)NotifyStateChanged();
        return true;
    }
    std::string encoded;
    try {encoded=SceneSaveSnapshot::Encode(_DirtyState);}
    catch(const std::exception& exception){error=exception.what();return false;}
    const QByteArray bytes=QByteArray::fromRawData(encoded.data(),static_cast<qsizetype>(encoded.size()));
    // 在提交文件前准备所有拥有内存的状态，提交成功后的替换不再分配字符串。
    std::string savedPath=normalized,savedHash=HashSceneBytes(bytes);
    Json::Value savedRoot=_DirtyState.CurrentRoot;
    auto savedOrder=_DirtyState.CurrentOrder;
    QSaveFile file(target);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {error=file.errorString().toStdString();return false;}
    if (file.write(bytes)!=bytes.size()) {error=file.errorString().toStdString();file.cancelWriting();return false;}
    // 另存为只允许创建新文件或覆盖当前场景，提交前再次检查外部创建/符号链接。
    QString checked;
    if (!Resolve(_Project,path,false,checked,error,true) || checked!=target ||
        (!exists && QFileInfo::exists(target))) {
        if (error.empty()) error="保存期间目标路径发生变化，未覆盖。";
        file.cancelWriting();return false;
    }
    if (currentPath && !DiskMatches(target,_DiskHash,error)) {file.cancelWriting();return false;}
    if (!file.commit()) {error=file.errorString().toStdString();return false;}
    _ActivePath.swap(savedPath);
    _DiskHash.swap(savedHash);
    // Reuse unchanged saved rows. Node handles transfer newly-created rows
    // without allocation after the atomic file commit; changed strings swap.
    for(const auto id:_DirtyState.DifferentNodes)
        if(!_DirtyState.ChangedNodes.count(id))_DirtyState.BaselineNodes.erase(id);
    while(!_DirtyState.ChangedNodes.empty()) {
        auto changed=_DirtyState.ChangedNodes.begin();
        const auto saved=_DirtyState.BaselineNodes.find(changed->first);
        if(saved==_DirtyState.BaselineNodes.end())_DirtyState.BaselineNodes.insert(_DirtyState.ChangedNodes.extract(changed));
        else {saved->second.swap(changed->second);_DirtyState.ChangedNodes.erase(changed);}
    }
    _DirtyState.BaselineRoot.swap(savedRoot);_DirtyState.BaselineOrder.swap(savedOrder);
    _DirtyState.DifferentNodes.clear();_DirtyState.RootDifferent=false;_DirtyState.OrderDifferent=false;
    _DirtyState.NotifiedDirty=false;_DirtyState.NotifiedRevision=_DirtyState.Revision;
    _Unsaved=false;_StatePending=false;
    RememberActiveScene();
    NotifyStateChanged();
    return true;
}

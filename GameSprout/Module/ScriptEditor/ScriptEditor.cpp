#include "ScriptEditor.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <exception>

namespace {
bool NativeSource(const std::string& path) {
    const QString extension=QFileInfo(QString::fromStdString(path)).suffix().toLower();
    return extension=="hpp" || extension=="h" || extension=="hxx" || extension=="inl" || extension=="cpp" || extension=="cc" || extension=="cxx";
}
struct CompileJob {
    ScriptCompileRequest Request;
    ScriptCompileResult Result;
};
}

void ScriptEditor::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    _Compiler=api->_EditorApi->GetGameSproutModule<IScriptCompiler>("ScriptCompiler");
    _Assets=api->_EditorApi->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Project=api->_EditorApi->GetGameSproutModule<IProject>("Project");
    _Guids=api->_EditorApi->GetGameSproutModule<IGuidCreator>("GuidCreator");
    _Console=api->_EditorApi->GetGameSproutModule<IConsole>("Console");
    _IDE=api->_EditorApi->GetGameSproutView<IIDEWindow>("IDEWindow");
    _Tasks=api->_Foundation->TaskApi();
    _Runtime=api->_EditorApi->_Runtime;
    _Scenes=api->_EditorApi->GetGameSproutModule<ISceneManager>("SceneManager");
    _Selection=api->_EditorApi->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _Editing=api->_EditorApi->GetGameSproutModule<IObjectEditing>("ObjectEditing");
}
bool ScriptEditor::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    return _Compiler && _Assets && _Project && _Guids && _Tasks && _Runtime && _IDE;
}
bool ScriptEditor::Launch(GameSproutEditor::GameSproutModuleApi*) {
    _Owner=_Tasks->RegisterTaskOwner();
    if (_Owner==Foundation::InvalidTaskOwnerId) return false;
    _Dispatch=new QObject();
    _Subscribed=_Assets->RegisterAssetListener(this);
    if(_Scenes)_FieldSceneSubscribed=_Scenes->RegisterSceneListener(this);
    _FieldRuntimeSubscribed=_Runtime->RegisterListener(this);
    return _Subscribed;
}
void ScriptEditor::LaunchFinished(GameSproutEditor::GameSproutModuleApi*) {
    // Subscribe before the initial snapshot. Future UI/catalog changes are event driven.
    for (const IAssetMeta* meta : _Assets->GetAssetMetas()) {
        if (meta && meta->_ImporterType==eAssetImporterType::Script && NativeSource(meta->GetPath()))
            _Sources.emplace(meta->GetPath(),ScriptCompileSource{meta->GetPath(),meta->_Guid.GetGuidString()});
    }
    _Launched=true;
    if (!_Sources.empty()) QueueCompile();
}
void ScriptEditor::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Launched=false;
    for(const auto id:_ObservedObjects)if(_Runtime)_Runtime->UnregisterObjectPropertyObserver(id,this);
    _ObservedObjects.clear();
    if(_FieldSceneSubscribed)_Scenes->UnregisterSceneListener(this);
    if(_FieldRuntimeSubscribed)_Runtime->UnregisterListener(this);
    _FieldSceneSubscribed=false;_FieldRuntimeSubscribed=false;_FieldObservers.clear();_Editing=nullptr;
    _ReloadBlocked=false; _CompileDeferred=false;
    if (_Subscribed) _Assets->UnregisterAssetListener(this);
    _Subscribed=false;
    delete _Dispatch; _Dispatch=nullptr; _Queued=false;
    if (_Owner!=Foundation::InvalidTaskOwnerId) _Tasks->UnregisterTaskOwner(_Owner);
    _Owner=Foundation::InvalidTaskOwnerId; _Task=Foundation::InvalidTaskId;
    if (_Loading) { _IDE->Unloading(); _Loading=false; }
    _State.Compiling=false;
    // Runtime must release gameplay instances and function closures while the module is loaded.
    for (auto registration=_Registrations.begin();registration!=_Registrations.end();) {
        std::string error;
        bool removed=false;
        try {removed=_Runtime->UnregisterScriptType(registration->Info.TypeID,error);}
        catch(const std::exception& exception) {error="Cannot release GameLogic registration: "+std::string(exception.what());}
        catch(...) {error="Cannot release GameLogic registration: unknown exception";}
        if (!removed) {
            if(error.empty()) error="Runtime rejected GameLogic registration release";
            _State.Error=error;Log(error,true);++registration;
        } else registration=_Registrations.erase(registration);
    }
    _Listeners.clear();_Sources.clear();
    if(!_Registrations.empty()) return; // Runtime still references this slot; a later Release can retry.
    _Types.clear();_CurrentSlot=-1;_LoadedFingerprint.clear();_LoadedLibraryHash.clear();
    for(int slot=0;slot<2;++slot) {
        std::string error;
        if(!RetireSlot(slot,error)) {_State.Error=error;Log(error,true);}
    }
    ReleaseSlotLock();
}
bool ScriptEditor::RegisterScriptListener(IScriptEditorListener* listener) {
    if (!listener) return false;
    for (const auto& entry:_Listeners) if (entry.second==listener) return false;
    _Listeners.emplace(++_ListenerID,listener); return true;
}
void ScriptEditor::UnregisterScriptListener(IScriptEditorListener* listener) {
    UnobserveFields(listener);
    for(auto entry=_Listeners.begin();entry!=_Listeners.end();) {
        if(entry->second==listener) entry=_Listeners.erase(entry); else ++entry;
    }
}
ScriptBuildState ScriptEditor::GetBuildState() const {
    ScriptBuildState state=_State;
    state.PendingChanges=_Queued||_CompileDeferred||_State.Compiling||_Revision!=_PublishedRevision;
    state.ReloadBlocked=_ReloadBlocked;
    state.SourceRevision=_SourceRevision;
    return state;
}
bool ScriptEditor::SetReloadBlocked(bool blocked,std::string& error) {
    error.clear();
    if(blocked==_ReloadBlocked) return true;
    if(blocked) {
        const auto state=GetBuildState();
        if(state.PendingChanges||!state.Error.empty()) {error="请等待最新游戏逻辑编译并加载成功后再运行。";return false;}
    } else if(_Runtime&&(_Runtime->IsExecuting()||_Runtime->IsScriptExecutionEnabled())) {
        error="游戏脚本尚未停止，不能解除模块重载保护。";return false;
    }
    _ReloadBlocked=blocked;
    if(!blocked&&(_CompileDeferred||_Revision!=_PublishedRevision)) {
        _CompileDeferred=false;
        if(_Launched) QueueCompile();
    }
    Notify(false);return true;
}
void ScriptEditor::Notify(bool types) {
    const auto listeners=_Listeners;
    const auto snapshot=_Types;
    const auto state=GetBuildState();
    for(const auto& [id,listener]:listeners) {
        const auto current=_Listeners.find(id);
        if(current==_Listeners.end() || current->second!=listener) continue;
        if(types) listener->OnScriptTypesChanged(snapshot);
        if(_Listeners.contains(id)) listener->OnScriptBuildStateChanged(state);
    }
}
void ScriptEditor::Log(const std::string& text,bool error) {
    if(_Console) _Console->ConsoleLog(__FILE__,__LINE__,error?eConsoleLog::Error:eConsoleLog::Trace,text);
    else if(error) qWarning().noquote()<<QString::fromStdString(text);
}
void ScriptEditor::OnAssetCreate(const AssetEvent& event) { SourceEvent(event,false); }
void ScriptEditor::OnAssetModify(const AssetEvent& event) { SourceEvent(event,false); }
void ScriptEditor::OnAssetRemove(const AssetEvent& event) { SourceEvent(event,true); }
void ScriptEditor::SourceEvent(const AssetEvent& event,bool remove) {
    bool changed=false;
    if(!event.OldPath.empty()) changed=_Sources.erase(event.OldPath)>0;
    if(remove) changed=(_Sources.erase(event.Path)>0)||changed;
    else if(event.Importer==eAssetImporterType::Script && NativeSource(event.Path)) {
        _Sources.insert_or_assign(event.Path,ScriptCompileSource{event.Path,event.GuidValue.GetGuidString()});
        changed=true; // Fingerprint catches metadata-only events without recompilation.
    }
    if(changed) { ++_Revision; ++_SourceRevision; if(_Launched) QueueCompile(); }
}
void ScriptEditor::QueueCompile() {
    if(_ReloadBlocked) {_CompileDeferred=true;Notify(false);return;}
    if(!_Dispatch || _Queued || _State.Compiling) return;
    _Queued=true;
    // One queued callback coalesces a burst of source notifications, not a frame poll.
    QTimer::singleShot(0,_Dispatch,[this]{ _Queued=false; if(_Launched) Compile(); });
}
bool ScriptEditor::RequestCompile(std::string& error) {
    error.clear();
    if(!_Launched) {error="ScriptEditor is not launched";return false;}
    ++_Revision; QueueCompile(); Notify(false); return true;
}
void ScriptEditor::Compile() {
    if(_State.Compiling || !_Launched) return;
    if(_ReloadBlocked) {_CompileDeferred=true;Notify(false);return;}
    ScriptCompileRequest request;
    request.LockWaitMilliseconds=60000;
    request.AssetsRoot=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets").toStdString();
    request.BuildRoot=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Library/ScriptBuild").toStdString();
    for(const auto& entry:_Sources) request.Sources.push_back(entry.second);
    if(request.Sources.empty() && !CurrentLibrary()) { _PublishedRevision=_Revision; _State.Error.clear(); Notify(false); return; }
    request.ArtifactRoot=QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Library/Artifact").toStdString();
    std::string assemblyError;
    if(!PrepareAssembly(assemblyError)) { _State.Error=assemblyError;Log(assemblyError,true);Notify(false);return; }
    request.AssemblyGuid=_AssemblyGuid;
    const uint64_t revision=_Revision;
    IScriptCompiler* const compiler=_Compiler;
    Foundation::TaskSubmitOptions options; options._OwnerId=_Owner;
    _IDE->Loading("编译游戏逻辑","GameLogic · C++"); _Loading=true;
    _State.Compiling=true; _State.Error.clear(); Notify(false);
    const auto submission=_Tasks->SubmitTask(options,
        [request,compiler](Int64& code,Foundation::ITaskContext& context) {
            auto* job=new CompileJob{request,{}};
            if(!context.SetOwned(job)) {delete job;code=-1;return false;}
            try {
                const bool success=compiler->Compile(request,job->Result,[&context]{return context.IsCancellationRequested();});
                code=success?0:-1; return success;
            } catch(const std::exception& exception) { job->Result.Diagnostics=exception.what();code=-1;return false; }
              catch(...) {job->Result.Diagnostics="Unknown native compiler exception";code=-1;return false;}
        },
        [this,revision](const Foundation::TaskCompletion&,bool success,const Foundation::ITaskContext& context) {
            _Task=Foundation::InvalidTaskId; _State.Compiling=false;
            if(_Loading) { _IDE->Unloading(); _Loading=false; }
            const auto* job=context.Get<CompileJob>();
            if(revision!=_Revision) {QueueCompile();Notify(false);return;} // Never publish a superseded source snapshot.
            std::string error;
            if(!success || !job) error=(job && !job->Result.Diagnostics.empty())?job->Result.Diagnostics:"Script compilation failed without diagnostics";
            else if(!Load(job->Request,job->Result,error)) success=false;
            if(!error.empty()) { _State.Error=error; Log("游戏逻辑编译/加载失败：\n"+error,true); }
            else {
                _PublishedRevision=revision;
                _State.HasSuccessfulBuild=true;
                if(!job->Result.ReflectionWarnings.empty() && _Console) _Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Warning,job->Result.ReflectionWarnings);
                Log(job->Result.CacheHit?"游戏逻辑缓存命中":"游戏逻辑编译并加载成功",false);
            }
            Notify(error.empty());
        });
    if(!submission.Accepted()) {
        _State.Compiling=false; _State.Error="TaskApi rejected GameLogic compilation";
        if(_Loading) {_IDE->Unloading();_Loading=false;} Log(_State.Error,true); Notify(false); return;
    }
    _Task=submission._TaskId;
}

#include "PrefabManager.h"
#include "PrefabRecords.h"
#include <QCoreApplication>

PrefabManager::~PrefabManager(){Release(nullptr);}
void PrefabManager::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    g_Self=this;g_GameSproutApi=api;
    if(!api||!api->_EditorApi)return;
    auto* editor=api->_EditorApi;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library=editor->GetGameSproutModule<ILibraryManager>("LibraryManager");
    _Project=editor->GetGameSproutModule<IProject>("Project");
    _Selection=editor->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _Console=editor->GetGameSproutModule<IConsole>("Console");_Runtime=editor->_Runtime;
}
bool PrefabManager::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _Dispatch=new QObject();return _Scenes&&_Assets&&_Library&&_Project&&_Runtime;
}
bool PrefabManager::Launch(GameSproutEditor::GameSproutModuleApi*) {
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);_AssetSubscribed=_Assets->RegisterAssetListener(this);
    return _SceneSubscribed&&_AssetSubscribed;
}
void PrefabManager::LaunchFinished(GameSproutEditor::GameSproutModuleApi*) {OnActiveSceneChanged();}
void PrefabManager::Release(GameSproutEditor::GameSproutModuleApi*) {
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_AssetSubscribed&&_Assets)_Assets->UnregisterAssetListener(this);
    _SceneSubscribed=false;_AssetSubscribed=false;delete _Dispatch;_Dispatch=nullptr;
    _Listeners.clear();_Errors.clear();_Sources.clear();_Pending.clear();_Queued=false;_SourceQueued=false;
    _Scenes=nullptr;_Assets=nullptr;_Library=nullptr;_Project=nullptr;_Runtime=nullptr;_Selection=nullptr;_Console=nullptr;
    if(g_Self==this)g_Self=nullptr;g_GameSproutApi=nullptr;
}
bool PrefabManager::Ready(std::string& error) const {
    error.clear();
    if(!_Scenes||!_Runtime||!_Project||!_Scenes->GetActiveScene()){error="当前没有可编辑的场景。";return false;}
    if(_Scenes->IsDocumentSessionActive()&&!_Scenes->IsEditingSuspendedDocument()) {
        error="独立编辑舞台中请直接编辑内容；本版不支持嵌套Prefab实例。";return false;
    }
    if(_Busy||_Runtime->IsExecuting()||_Scenes->HasTransformEdit()||_Scenes->IsPlaySessionActive()) {
        error="请先结束运行、变换拖拽或当前Prefab事务。";return false;
    }
    return true;
}
bool PrefabManager::RegisterPrefabListener(IPrefabListener* listener) {
    if(!listener)return false;for(const auto& [id,value]:_Listeners)if(value==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void PrefabManager::UnregisterPrefabListener(IPrefabListener* listener) {
    for(auto it=_Listeners.begin();it!=_Listeners.end();)if(it->second==listener)it=_Listeners.erase(it);else ++it;
}
void PrefabManager::Notify() {
    std::vector<uint64_t> ids;for(const auto& [id,listener]:_Listeners)ids.push_back(id);
    for(const auto id:ids){const auto found=_Listeners.find(id);if(found!=_Listeners.end())found->second->OnPrefabStateChanged();}
}
void PrefabManager::QueueNotify() {
    if(!_Dispatch||_Queued)return;_Queued=true;
    QMetaObject::invokeMethod(_Dispatch,[this]{_Queued=false;Notify();},Qt::QueuedConnection);
}
void PrefabManager::OnActiveSceneChanged() {
    ++_SceneEpoch;_Pending.clear();_Errors.clear();QueueNotify();
    if(!_Scenes||_Scenes->IsPlaySessionActive())return;
    try {
        const auto annotations=PrefabRecords::Parse(_Scenes->GetSceneAnnotationsJson());
        for(const auto& record:annotations["PrefabInstances"])
            QueueSource(AssetEvent(eAssetMetaEvent::Modify,Guid(record["Source"]["Guid"].asString()),{}, {},eAssetImporterType::Prefab,eAssetImportState::Ready,{}));
    }catch(const std::exception& exception){if(_Console)_Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Error,exception.what());}
}
void PrefabManager::OnSceneStateChanged(){RetryPending();QueueNotify();}
void PrefabManager::OnPlaySessionChanged(bool active){if(!active)OnActiveSceneChanged();else {_Pending.clear();QueueNotify();}}
void PrefabManager::OnAssetCreate(const AssetEvent& event){QueueSource(event);}
void PrefabManager::OnAssetModify(const AssetEvent& event){QueueSource(event);}
void PrefabManager::OnAssetRemove(const AssetEvent& event){QueueSource(event);}
void PrefabManager::QueueSource(const AssetEvent& event) {
    if(event.Importer!=eAssetImporterType::Prefab)return;
    if(event.Action==eAssetMetaEvent::Remove){_Pending.erase(event.Path);if(!event.OldPath.empty())_Pending.erase(event.OldPath);}
    QueueNotify();
    if(event.Action!=eAssetMetaEvent::Remove&&event.State!=eAssetImportState::Ready)return;
    _Sources.insert(event.GuidValue.GetGuidString());
    if(_Scenes&&_Scenes->IsDocumentSessionActive()&&!_Scenes->IsEditingSuspendedDocument())return;
    if(!_Dispatch||_SourceQueued)return;_SourceQueued=true;
    QMetaObject::invokeMethod(_Dispatch,[this]{
        _SourceQueued=false;const auto sources=std::move(_Sources);_Sources.clear();
        if(!_Scenes||_Scenes->IsPlaySessionActive())return;
        if(_Scenes->IsDocumentSessionActive()&&!_Scenes->IsEditingSuspendedDocument()) {
            _Sources.insert(sources.begin(),sources.end());return;
        }
        for(const auto& guid:sources){CompletePending(guid);std::string error;if(!RefreshSource(guid,error)&&_Console)_Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Warning,error);}
    },Qt::QueuedConnection);
}

#include "PrefabStage.h"
#include "IPrefabManager.h"
#include "IProject.h"
#include "IPlayMode.h"
#include "IAnimationPreview.h"
#include "IRuntimeHost.h"
#include "../Shared/PrefabDefinition.h"
#include "../Shared/AssetArtifactIO.h"
#include <QCoreApplication>
#include <QDebug>
#include <limits>

namespace {
class StageOperation {
public:
    explicit StageOperation(bool& busy):_Busy(busy){_Busy=true;}
    ~StageOperation(){_Busy=false;}
private:bool& _Busy;
};
}
PrefabStage::~PrefabStage(){Release(nullptr);}
void PrefabStage::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    g_Self=this;g_GameSproutApi=api;
    auto* editor=api?api->_EditorApi:nullptr;if(!editor)return;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Project=editor->GetGameSproutModule<IProject>("Project");
    _Prefabs=editor->GetGameSproutModule<IPrefabManager>("PrefabManager");
    _Selection=editor->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Preview=editor->GetGameSproutModule<IAnimationPreview>("AnimationPreview");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");_Runtime=editor->_Runtime;
}
bool PrefabStage::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    if(!_Dispatch)_Dispatch=new QObject();
    return _Scenes&&_Assets&&_Project&&_Prefabs&&_Runtime;
}
bool PrefabStage::Launch(GameSproutEditor::GameSproutModuleApi*) {
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    _AssetSubscribed=_Assets->RegisterAssetListener(this);return _SceneSubscribed&&_AssetSubscribed;
}
bool PrefabStage::Ready(std::string& error) const {
    error.clear();
    if(!_Scenes||!_Runtime||!_Project||!_Assets||!_Prefabs){error="Prefab Stage服务尚未就绪。";return false;}
    if(_Busy||_Notifying||_Runtime->IsExecuting()||(_Host&&_Host->IsFrameActive())||_Scenes->HasTransformEdit()) {
        error="请先完成当前编辑或绘制操作，再切换/保存Prefab Stage。";return false;
    }
    if(_Scenes->IsPlaySessionActive()||_Runtime->IsScriptExecutionEnabled()||(_Play&&_Play->GetState().State!=EditorPlayState::Edit)) {
        error="运行模式期间不能编辑Prefab Stage。";return false;
    }
    return true;
}
bool PrefabStage::OpenPrefab(const std::string& guid,std::string& error) {
    if(!Ready(error))return false;
    if(_State.Active&&_State.Guid==guid)return true;
    if(_State.Active&&_State.Dirty){error="请先保存或明确放弃当前Prefab修改。";return false;}
    Json::Value data;std::string path,hash;
    // Validate before closing a current document; malformed sources never disturb it.
    if(!ReadSource(guid,data,path,hash,error))return false;
    if(_State.Active&&!Close(false,error))return false;
    if(_Scenes->IsDocumentSessionActive()){error="已有其他独立编辑文档。";return false;}
    if(_Preview)_Preview->ResetAnimationPreview();
    if(_Selection){_PreviousSceneSelection=_Selection->GetSceneObjectSelection();_PreviousAssets=_Selection->GetAssetSelection();}
    StageOperation operation(_Busy);
    SceneDocumentSessionRequest request;request.DocumentJson=AssetArtifactIO::JsonText(data);
    request.Name=data["Name"].asString();request.ContentRootFileID=PrefabDefinition::RootID(data);
    if(!_Scenes->BeginDocumentSession(request,error))return false;
    auto* scene=_Scenes->GetActiveScene();auto* root=_Scenes->GetDocumentContentRoot();
    _State={};_State.Active=true;_State.Guid=guid;_State.Path=path;
    _State.SceneID=scene?scene->GetInstanceID():0;_State.ContentRootObjectID=root?root->GetInstanceID():0;
    _DiskHash=hash;
    Json::Value current;
    if(!Capture(current,_SavedText,error)||!_Scenes->MarkDocumentSessionSaved(error)) {
        std::string cleanup;_Scenes->EndDocumentSession(cleanup);_State={};RestoreSelection();return false;
    }
    if(_Selection)_Selection->SetSceneObjectSelection({_State.SceneID,_State.ContentRootObjectID});
    Notify();return true;
}
bool PrefabStage::Save(std::string& error) {
    if(!Ready(error))return false;
    if(!_State.Active){error="没有打开Prefab Stage。";return false;}
    if(_Preview)_Preview->ResetAnimationPreview();
    StageOperation operation(_Busy);
    Json::Value data;std::string current;
    if(!Capture(data,current,error)){_State.Dirty=true;_State.Error=error;Notify();return false;}
    _State.Dirty=current!=_SavedText;
    Json::Value disk;std::string path,hash;
    if(!ReadSource(_State.Guid,disk,path,hash,error)) {_State.MissingSource=true;_State.Error=error;Notify();return false;}
    _State.Path=path;
    if(hash!=_DiskHash){_State.ExternalConflict=true;_State.Error="Prefab源已被外部修改。请保留当前编辑，或退出放弃后重新打开。";error=_State.Error;Notify();return false;}
    std::string savedHash;
    if(!WriteSource(data,hash,savedHash,error)){_State.Error=error;Notify();return false;}
    _DiskHash=savedHash;_SavedText=current;_State.Dirty=false;_State.MissingSource=false;_State.ExternalConflict=false;_State.Error.clear();
    if(!_Scenes->MarkDocumentSessionSaved(error)){_State.Error="源已保存，但舞台保存基线更新失败："+error;Notify();return false;}
    // The main world stays alive. Its independent author context is edited via
    // the public transaction boundary; Stage controls never see that context.
    std::string refresh;
    if(!_Scenes->EditSuspendedDocument([this](std::string& message){return _Prefabs->RefreshSource(_State.Guid,message);},refresh))
        _State.Error="Prefab已保存；部分场景实例更新冲突："+refresh;
    error.clear();Notify();return true;
}
bool PrefabStage::Close(bool discard,std::string& error) {
    if(!Ready(error))return false;
    if(!_State.Active)return true;
    if(_Preview)_Preview->ResetAnimationPreview();
    Json::Value current;std::string text;
    if(Capture(current,text,error))_State.Dirty=text!=_SavedText;
    else {_State.Dirty=true;_State.Error=error;}
    if(_State.Dirty&&!discard){error="Prefab尚有未保存修改。请保存、放弃或取消。";Notify();return false;}
    StageOperation operation(_Busy);
    if(!_Scenes->EndDocumentSession(error))return false;
    _State={};_SavedText.clear();_DiskHash.clear();RestoreSelection();Notify();return true;
}
void PrefabStage::RestoreSelection() {
    if(!_Selection)return;
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    auto* object=_Runtime?_Runtime->QueryGameObject(_PreviousSceneSelection.ObjectID):nullptr;
    if(scene&&object&&object->GetScene()==scene&&_PreviousSceneSelection.SceneID==scene->GetInstanceID())
        _Selection->SetSceneObjectSelection(_PreviousSceneSelection);
    else _Selection->SetAssetSelection(_PreviousAssets);
    _PreviousSceneSelection={};_PreviousAssets.clear();
}
void PrefabStage::OnActiveSceneChanged() {
    if(_Busy||!_State.Active)return;
    if(!_Scenes->IsDocumentSessionActive()||!_Scenes->GetActiveScene()||_Scenes->GetActiveScene()->GetInstanceID()!=_State.SceneID) {
        _State={};_SavedText.clear();_DiskHash.clear();Notify();
    }
}
void PrefabStage::OnSceneStateChanged() {
    if(_Busy||!_State.Active||_Scenes->IsEditingSuspendedDocument())return;
    Json::Value current;std::string text,error;
    if(Capture(current,text,error)){_State.Dirty=text!=_SavedText;if(!_State.MissingSource&&!_State.ExternalConflict)_State.Error.clear();}
    else {_State.Dirty=true;_State.Error=error;}
    Notify();
}
void PrefabStage::QueueObserve(const AssetEvent& event) {
    if(!_State.Active||event.GuidValue.GetGuidString()!=_State.Guid||!_Dispatch||_Observing)return;
    _Observing=true;QMetaObject::invokeMethod(_Dispatch,[this]{_Observing=false;ObserveSource();},Qt::QueuedConnection);
}
void PrefabStage::ObserveSource() {
    if(!_State.Active)return;
    Json::Value data;std::string path,hash,error;
    if(ReadSource(_State.Guid,data,path,hash,error)) {
        _State.Path=path;_State.MissingSource=false;_State.ExternalConflict=hash!=_DiskHash;
        _State.Error=_State.ExternalConflict?"Prefab源已被外部修改；为避免覆盖，请退出放弃后重新打开。":"";
    }else {_State.MissingSource=true;_State.Error=error;}
    Notify();
}
void PrefabStage::OnAssetCreate(const AssetEvent& event){QueueObserve(event);}
void PrefabStage::OnAssetModify(const AssetEvent& event){QueueObserve(event);}
void PrefabStage::OnAssetRemove(const AssetEvent& event){QueueObserve(event);}
bool PrefabStage::RegisterStageListener(IPrefabStageListener* listener) {
    if(!listener||_NextListener==std::numeric_limits<uint64_t>::max())return false;
    for(const auto& [id,value]:_Listeners)if(value==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void PrefabStage::UnregisterStageListener(IPrefabStageListener* listener) {
    for(auto it=_Listeners.begin();it!=_Listeners.end();)if(it->second==listener)it=_Listeners.erase(it);else ++it;
}
void PrefabStage::Notify() {
    if(_Notifying)return;_Notifying=true;const auto state=_State;const auto last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last){const auto id=current->first;try{current->second->OnPrefabStageChanged(state);}catch(...){qWarning("Prefab Stage listener threw");}current=_Listeners.upper_bound(id);}
    _Notifying=false;
}
void PrefabStage::Release(GameSproutEditor::GameSproutModuleApi*) {
    if(_State.Active&&_Scenes){std::string error;if(!Close(true,error))qWarning().noquote()<<QString::fromStdString(error);}
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_AssetSubscribed&&_Assets)_Assets->UnregisterAssetListener(this);
    _SceneSubscribed=_AssetSubscribed=false;delete _Dispatch;_Dispatch=nullptr;
    _Listeners.clear();_State={};_Scenes=nullptr;_Assets=nullptr;_Project=nullptr;_Prefabs=nullptr;_Selection=nullptr;
    _Play=nullptr;_Preview=nullptr;_Host=nullptr;_Runtime=nullptr;_Observing=false;_Busy=false;
    if(g_Self==this)g_Self=nullptr;g_GameSproutApi=nullptr;
}

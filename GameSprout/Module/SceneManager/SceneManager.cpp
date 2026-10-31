#include "SceneManager.h"
#include "SceneDocument.h"
#include "IRuntimeAssets.h"
#include "GameSproutRuntimeExport.h"
#include "SafeSystem.hpp"
#include <QDebug>
#include <limits>

SceneManager::~SceneManager() { Release(nullptr); }
void SceneManager::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    _RuntimeAssets=api && api->_EditorApi ? api->_EditorApi->GetGameSproutModule<IRuntimeAssets>("RuntimeAssets") : nullptr;
    _Project=api && api->_EditorApi ? api->_EditorApi->GetGameSproutModule<IProject>("Project") : nullptr;
}
bool SceneManager::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    if(_Runtime) return true;
    if(!_Project) return false;
    _Runtime=Export(FoundationUtils::SafeSystem::File::GetDirectoryOfCurrentExe());
    if(!_Runtime) {
        qCritical() << "Cannot load GameSproutRuntime from the executable directory.";
        return false;
    }
    _RuntimeSubscribed=_Runtime->RegisterListener(this);
    return _RuntimeSubscribed;
}
bool SceneManager::Launch(GameSproutEditor::GameSproutModuleApi*) {
    if(!_Runtime) return false;
    if (_RuntimeAssets) _Runtime->SetResourceProvider(_RuntimeAssets->GetResourceProvider());
    if(!GetActiveScene()) {
        GameSproutRuntime::IScene* scene=nullptr;
        SceneDocument::ObjectIDs ids;
        std::string error;
        if(!SceneDocument::Instantiate(SceneDocument::CreateDefault("Untitled"),*_Runtime,scene,ids,error)) {
            qCritical().noquote() << QString::fromStdString(error);return false;
        }
        Activate(scene,"",std::move(ids));
    }
    _Timer.start();
    return true;
}
GameSproutRuntime::IScene* SceneManager::GetActiveScene() const {
    return _Runtime ? dynamic_cast<GameSproutRuntime::IScene*>(_Runtime->QueryGameObject(_ActiveID)) : nullptr;
}
void SceneManager::Activate(GameSproutRuntime::IScene* scene,std::string path,SceneDocument::ObjectIDs ids,std::string diskHash) {
    GameSproutRuntime::IScene* previous=GetActiveScene();
    ClearAnimationClipPreview();
    _TransformEdit={};_AnimationPreviewPlaying=false;
    _ActiveID=scene?scene->GetInstanceID():0;
    _ActivePath.swap(path);
    _DiskHash.swap(diskHash);
    _ObjectIDs=std::move(ids);_History.clear();_HistoryPosition=0;_BaselineText.clear();_LastStateText.clear();
    Json::Value document;std::string text,error;
    if (Capture(document,text,error)) {
        _LastStateText=text;
        if (!_ActivePath.empty()) _BaselineText=text;
    }
    _Unsaved=_ActivePath.empty();_StatePending=false;
    // 先让 View 切到新身份，再释放旧场景；View 不保存旧节点裸指针。
    NotifySceneChanged();
    if(previous && previous!=scene) _Runtime->DestroyScene(previous);
}
void SceneManager::Update(GameSproutEditor::GameSproutModuleApi*) {
    if(!_Runtime) return;
    const float elapsed=_Timer.isValid()?static_cast<float>(_Timer.nsecsElapsed()/1000000000.0):0.0f;
    _Timer.start();
    const auto clip=GetAnimationClipPreviewState(_ClipPreviewObjectID);
    if(!clip.Active&&_ClipPreviewObjectID)ClearAnimationClipPreview();
    // 预览期间仍允许创建节点；新增 Animator 也不能随选中 Clip 的时钟自动运行。
    if(clip.Active)PauseControllerAnimators(_ClipPreviewObjectID);
    _Runtime->Update((_AnimationPreviewPlaying||(clip.Active&&clip.Playing))?elapsed:0.0f);
    if(_ClipPreviewObjectID&&!GetAnimationClipPreviewState(_ClipPreviewObjectID).Active)ClearAnimationClipPreview();
    if(_TransformEdit.Active&&!TransformTarget(true)) {std::string error;CancelTransformEdit(error);}
    if (_StatePending) RefreshState();
}
void SceneManager::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Listeners.clear();
    ClearAnimationClipPreview();
    if(_TransformEdit.Active){std::string error;CancelTransformEdit(error);}
    _TransformEdit={};
    if(_RuntimeSubscribed && _Runtime) _Runtime->UnregisterListener(this);
    _RuntimeSubscribed=false;
    if(_Runtime)_Runtime->SetResourceProvider(nullptr);
    delete _Runtime;_Runtime=nullptr;
    _Project=nullptr;_RuntimeAssets=nullptr;_AnimationPreviewPlaying=false;_ActiveID=0;_ActivePath.clear();_Timer.invalidate();
    _Switching=false;_Notifying=false;_NotifyAgain=false;_StateNotifyAgain=false;_ExecutingEdit=false;_StatePending=false;
    _ObjectIDs={};_BaselineText.clear();_DiskHash.clear();_LastStateText.clear();_History.clear();_HistoryPosition=0;_Unsaved=true;
}
bool SceneManager::RegisterSceneListener(ISceneListener* listener) {
    if(!listener || _NextListener==std::numeric_limits<std::uint64_t>::max()) return false;
    for(const auto& entry:_Listeners) if(entry.second==listener) return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void SceneManager::UnregisterSceneListener(ISceneListener* listener) {
    for(auto it=_Listeners.begin();it!=_Listeners.end();) {
        if(it->second==listener) it=_Listeners.erase(it);else ++it;
    }
}
void SceneManager::NotifySceneChanged() {
    NotifyListeners(true);
}
void SceneManager::NotifyStateChanged() {
    NotifyListeners(false);
}
void SceneManager::NotifyListeners(bool active) {
    if(_Notifying) {if(active) _NotifyAgain=true;else _StateNotifyAgain=true;return;}
    _Notifying=true;
    bool state=!active;
    do {
        _NotifyAgain=false;_StateNotifyAgain=false;
        // 只通知本轮开始时已有的监听者；回调可以安全地注销自己或其他监听者。
        const std::uint64_t lastListener=_NextListener;
        auto current=_Listeners.begin();
        while(current!=_Listeners.end() && current->first<=lastListener) {
            const std::uint64_t id=current->first;
            try { if(active) current->second->OnActiveSceneChanged(); }
            catch (...) { qWarning("Scene listener threw an exception"); }
            current=_Listeners.find(id);
            if(state && current!=_Listeners.end()) {
                try {current->second->OnSceneStateChanged();}
                catch (...) {qWarning("Scene state listener threw an exception");}
            }
            current=_Listeners.upper_bound(id);
        }
        active=_NotifyAgain;state=_StateNotifyAgain;
    } while(active || state);
    _Notifying=false;
}
void SceneManager::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    if(event.Type==GameSproutRuntime::SceneNodeEventType::Destroy && event.ObjectID==_ClipPreviewObjectID)
        _ClipPreviewObjectID=0;
    if(event.Type==GameSproutRuntime::SceneNodeEventType::Destroy && event.ObjectID==_ActiveID) {
        _ClipPreviewObjectID=0;_AnimationPreviewPlaying=false;
        _TransformEdit={};
        _ActiveID=0;_ActivePath.clear();_ObjectIDs={};_History.clear();_HistoryPosition=0;
        _BaselineText.clear();_DiskHash.clear();_LastStateText.clear();_Unsaved=false;_StatePending=false;NotifySceneChanged();
    } else if(event.SceneID==_ActiveID && event.Type!=GameSproutRuntime::SceneNodeEventType::OperationFailed) _StatePending=true;
}

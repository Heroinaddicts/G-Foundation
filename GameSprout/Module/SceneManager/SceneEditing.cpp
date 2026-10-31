#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <exception>

namespace {
class EditingScope {
public:
    explicit EditingScope(bool& flag):_Flag(flag) {_Flag=true;}
    ~EditingScope() {_Flag=false;}
private:
    bool& _Flag;
};
}

bool SceneManager::Ready(std::string& error) const {
    error.clear();
    if (!_Runtime || !GetActiveScene()) {error="当前没有可操作的场景。";return false;}
    if (_TransformEdit.Active) {error="请先完成或取消 Scene 中的变换拖拽。";return false;}
    if (_Switching || _ExecutingEdit || _Notifying || _Runtime->IsExecuting()) {
        error="场景或 Runtime 正在执行其他操作，不能重入编辑、撤销或保存。";return false;
    }
    return true;
}
bool SceneManager::Capture(Json::Value& document,std::string& text,std::string& error) const {
    auto* scene=GetActiveScene();
    if (!scene || !_Runtime || _Runtime->IsExecuting()) {error="场景不可用或 Runtime 正在执行回调。";return false;}
    try {
        if (!SceneDocument::Serialize(*scene,_ObjectIDs,document,error)) return false;
        text=AssetArtifactIO::JsonText(document);return true;
    }
    catch (const std::exception& exception) {error=exception.what();return false;}
    catch (...) {error="无法生成场景数据。";return false;}
}
bool SceneManager::HasUnsavedChanges() const {
    if (!GetActiveScene()) return false;
    if (_Runtime->IsExecuting()) return _Unsaved;
    Json::Value document;std::string text,error;
    _Unsaved=_ActivePath.empty() || !Capture(document,text,error) || text!=_BaselineText;
    return _Unsaved;
}
void SceneManager::RefreshState(bool force) {
    _StatePending=false;
    Json::Value document;std::string text,error;
    const bool captured=Capture(document,text,error);
    if (!captured) text="Invalid scene: "+error;
    const bool dirty=GetActiveScene() && (_ActivePath.empty() || !captured || text!=_BaselineText);
    const bool changed=force || text!=_LastStateText || dirty!=_Unsaved;
    _Unsaved=dirty;_LastStateText.swap(text);
    if (changed) NotifyStateChanged();
}
bool SceneManager::RunEdit(const Edit& edit,bool forward,std::string& error) {
    if(edit.Type==Edit::Kind::Deletion)return RunDeletion(edit,forward,error);
    if(edit.Type==Edit::Kind::Creation)return RunDeletion(edit,!forward,error);
    const auto sceneID=_ActiveID;
    auto resolve=[&]() -> GameSproutRuntime::IGameObject* {
        auto* scene=GetActiveScene();
        auto* object=scene && _ActiveID==sceneID?QueryObjectByFileID(edit.FileID):nullptr;
        return object && object->GetScene()==scene?object:nullptr;
    };
    auto* object=resolve();
    if (!object) {error="编辑目标已经删除或不属于当前场景。";return false;}
    const auto& operation=forward?edit.Apply:edit.Revert;
    const auto& rollback=forward?edit.Revert:edit.Apply;
    bool accepted=false;
    try {accepted=operation(*object,error);}
    catch (const std::exception& exception) {error=exception.what();}
    catch (...) {error="属性操作抛出异常。";}
    object=resolve();
    if (accepted && object) {
        Json::Value document;std::string text;
        if (Capture(document,text,error)) return true;
    }
    if (error.empty()) error=object?"属性操作失败。":"属性操作删除了编辑目标，不能记录撤销。";
    // 公共 setters 通常原子拒绝；也恢复误先写入再返回 false 的属性操作。
    if (object) {
        std::string rollbackError;
        try {if (!rollback(*object,rollbackError)) error+=" 恢复原值失败："+rollbackError;}
        catch (...) {error+=" 恢复原值时发生异常。";}
    }
    return false;
}
bool SceneManager::EditObject(uint64_t objectID,const std::string& label,const fSceneEditOperation& apply,
                              const fSceneEditOperation& revert,std::string& error) {
    if (!Ready(error)) return false;
    if (!objectID || !apply || !revert) {error="编辑需要有效对象和正反操作。";return false;}
    auto* object=_Runtime->QueryGameObject(objectID);auto* scene=GetActiveScene();
    if(!object||object->GetScene()!=scene){error="编辑目标已经删除或不属于当前场景。";return false;}
    Json::Value document;std::string text;
    if(!Capture(document,text,error))return false;
    Edit edit;
    try {
        edit.FileID=object==scene?0:_ObjectIDs.RuntimeToFile.at(objectID);edit.Label=label;edit.Apply=apply;edit.Revert=revert;
        _History.reserve(_HistoryPosition+1);
    }
    catch (...) {error="无法分配撤销记录。";return false;}
    EditingScope editing(_ExecutingEdit);
    const bool success=RunEdit(edit,true,error);
    if (success) {
        _History.resize(_HistoryPosition);_History.push_back(std::move(edit));++_HistoryPosition;
    }
    RefreshState(true);
    return success;
}
bool SceneManager::Undo(std::string& error) {
    if (!Ready(error)) return false;
    if (!CanUndo()) {error="没有可撤销的属性操作。";return false;}
    EditingScope editing(_ExecutingEdit);
    const bool success=RunEdit(_History[_HistoryPosition-1],false,error);
    if (success) --_HistoryPosition;
    RefreshState(true);
    return success;
}
bool SceneManager::Redo(std::string& error) {
    if (!Ready(error)) return false;
    if (!CanRedo()) {error="没有可重做的属性操作。";return false;}
    EditingScope editing(_ExecutingEdit);
    const bool success=RunEdit(_History[_HistoryPosition],true,error);
    if (success) ++_HistoryPosition;
    RefreshState(true);
    return success;
}

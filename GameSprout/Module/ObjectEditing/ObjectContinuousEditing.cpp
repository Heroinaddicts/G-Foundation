#include "ObjectEditing.h"
#include "IRuntimeHost.h"
#include <exception>
#include <QObject>
#include <QMetaObject>

namespace {
struct ContinuousScope {
    bool& Busy;
    explicit ContinuousScope(bool& value):Busy(value){Busy=true;}
    ~ContinuousScope(){Busy=false;}
};
}
bool ObjectEditing::ContinuousReady(std::string& error) const {
    error.clear();
    if(!OwnerThread()||!_Ready||!_Continuous.Active||_Busy||!_Runtime||_Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="连续属性编辑需要所属主线程的空闲阶段。";return false;
    }
    if(!Resolve(_Continuous.Target)||_Scenes->IsPlaySessionActive()!=_Continuous.Playing) {
        error="连续属性目标已经失效或所属世界已切换。";return false;
    }
    const auto mode=_Play->GetState().State;
    if((_Continuous.Playing&&mode!=EditorPlayState::Playing&&mode!=EditorPlayState::Paused)
        ||(!_Continuous.Playing&&mode!=EditorPlayState::Edit)) {
        error="运行模式正在切换，不能继续预览属性。";return false;
    }
    return true;
}
bool ObjectEditing::BeginContinuousEdit(const ObjectEditTarget& target,const std::string& label,
    const ISceneManager::fSceneEditOperation& restore,std::string& error) {
    if(!CanEdit(target,error))return false;
    if(!restore){error="连续属性编辑需要原值恢复操作。";return false;}
    ContinuousEdit candidate;candidate.Active=true;candidate.Target=target;candidate.Label=label;
    candidate.Playing=_Scenes->IsPlaySessionActive();candidate.Restore=restore;
    if(!candidate.Playing&&!_Scenes->BeginPropertyPreview(error))return false;
    _Continuous=std::move(candidate);return true;
}
bool ObjectEditing::PreviewContinuousEdit(const ISceneManager::fSceneEditOperation& apply,
    const ISceneManager::fSceneEditOperation& rollback,std::string& error) {
    if(!ContinuousReady(error))return false;
    if(!apply||!rollback){error="属性预览需要有效的修改和恢复操作。";return false;}
    ContinuousScope scope(_Busy);const auto target=_Continuous.Target;
    bool accepted=false;
    try {
        accepted=_Continuous.Playing?TransientEdit(target,apply,rollback,error):apply(*Resolve(target),error);
        if(accepted&&!Resolve(target)){error="属性预览不能删除或迁移目标。";accepted=false;}
        if(accepted&&!_Continuous.Playing) {
            accepted=_Scenes->ValidatePropertyPreview(error);
        }
    }catch(const std::exception& exception){accepted=false;error=exception.what();}
     catch(...){accepted=false;error="预览属性时发生异常。";}
    if(!accepted&&!_Continuous.Playing) {
        if(auto* object=Resolve(target)) {
            std::string ignored;try {rollback(*object,ignored);}catch(...) {}
        }
    }
    if(!_Continuous.Playing)_Scenes->PublishPropertyPreview();
    if(accepted)Notify(target,_Continuous.Playing);
    return accepted;
}
bool ObjectEditing::CommitContinuousEdit(const ISceneManager::fSceneEditOperation& apply,std::string& error) {
    if(!ContinuousReady(error))return false;
    if(!apply){error="连续属性提交缺少最终操作。";return false;}
    auto completed=std::move(_Continuous);_Continuous={};
    if(completed.Playing) {ContinuousScope scope(_Busy);Notify(completed.Target,true);return true;}
    _Scenes->EndPropertyPreview();
    bool restored=false;
    try {ContinuousScope scope(_Busy);if(auto* object=Resolve(completed.Target))restored=completed.Restore(*object,error);}
    catch(...){error="恢复预览起点时发生异常。";}
    if(!restored)return false;
    return EditObject(completed.Target,completed.Label,apply,completed.Restore,error);
}
bool ObjectEditing::CancelContinuousEdit(std::string& error) {
    error.clear();if(!_Continuous.Active)return true;
    if(!OwnerThread()||_Busy){error="当前回调中不能取消属性预览。";return false;}
    // Frame callbacks may request cancellation, but must not discard the
    // original state while Runtime still owns the target. Retry once through
    // the owner event loop; no timer/Update polling is introduced.
    if(Resolve(_Continuous.Target)&&(_Runtime->IsExecuting()||(_Host&&_Host->IsFrameActive()))) {
        error="Runtime 正在使用目标，取消将在主线程安全点恢复原值。";
        if(!_CancelQueued&&!_CancelRetry&&_Dispatch) {
            _CancelQueued=true;
            QMetaObject::invokeMethod(_Dispatch,[this] {
                _CancelQueued=false;_CancelRetry=true;
                std::string ignored;CancelContinuousEdit(ignored);_CancelRetry=false;
            },Qt::QueuedConnection);
        }
        return false;
    }
    auto cancelled=std::move(_Continuous);_Continuous={};
    bool restored=true;
    if(auto* object=Resolve(cancelled.Target)) {
        {
            ContinuousScope scope(_Busy);
            try {
                restored=cancelled.Restore(*object,error);
                if(restored&&cancelled.Playing)restored=_Runtime->SynchronizePhysics(error);
            }catch(const std::exception& exception){restored=false;error=exception.what();}
             catch(...){restored=false;error="取消属性预览时发生异常。";}
        }
    }
    if(!cancelled.Playing) {
        _Scenes->PublishPropertyPreview();_Scenes->EndPropertyPreview();
    }
    {ContinuousScope scope(_Busy);Notify(cancelled.Target,cancelled.Playing);}return restored;
}
void ObjectEditing::OnPlayModeStateChanged(const PlayModeState& state) {
    if(state.State==EditorPlayState::EnteringPlay||state.State==EditorPlayState::ExitingPlay) {
        std::string ignored;CancelContinuousEdit(ignored);
    }
}
void ObjectEditing::OnActiveSceneChanged() {
    if(_Continuous.Active){std::string ignored;CancelContinuousEdit(ignored);}
}
void ObjectEditing::OnSceneStateChanged() {
    if(_Continuous.Active&&!_Busy&&!Resolve(_Continuous.Target)) {
        std::string ignored;CancelContinuousEdit(ignored);
    }
}
void ObjectEditing::OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>& selection) {
    if(_Continuous.Active&&!selection.empty()){std::string ignored;CancelContinuousEdit(ignored);}
}
void ObjectEditing::OnSceneObjectSelectionChanged(const SceneObjectSelection& selection) {
    if(_Continuous.Active&&(selection.SceneID!=_Continuous.Target.SceneID||selection.ObjectID!=_Continuous.Target.ObjectID)) {
        std::string ignored;CancelContinuousEdit(ignored);
    }
}

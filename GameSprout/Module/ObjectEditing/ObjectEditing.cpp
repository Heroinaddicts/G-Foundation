#include "ObjectEditing.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include <QThread>
#include <QDebug>
#include <exception>
#include <limits>
#include <QObject>

namespace {
struct EditingScope {
    bool& Busy;
    explicit EditingScope(bool& value):Busy(value){Busy=true;}
    ~EditingScope(){Busy=false;}
};
}
ObjectEditing::~ObjectEditing(){Release(nullptr);}
void ObjectEditing::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    _Scenes=editor?editor->GetGameSproutModule<ISceneManager>("SceneManager"):nullptr;
    _Play=editor?editor->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    _Host=editor?editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost"):nullptr;
    _Runtime=editor?editor->_Runtime:nullptr;
    _Selection=editor?editor->GetGameSproutModule<ISelectionManager>("SelectionManager"):nullptr;
}
bool ObjectEditing::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();
    _Dispatch=new QObject();
    _Ready=_Scenes&&_Play&&_Host&&_Runtime;
    return _Ready;
}
bool ObjectEditing::Launch(GameSproutEditor::GameSproutModuleApi*) {
    if(_Ready) {
        _PlaySubscribed=_Play->RegisterPlayModeListener(this);
        _SceneSubscribed=_Scenes->RegisterSceneListener(this);
        if(_Selection)_SelectionSubscribed=_Selection->RegisterSelectionListener(this);
    }
    return _Ready;
}
bool ObjectEditing::OwnerThread() const {return _OwnerThread&&QThread::currentThread()==_OwnerThread;}
GameSproutRuntime::IGameObject* ObjectEditing::Resolve(const ObjectEditTarget& target) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    auto* object=_Runtime?_Runtime->QueryGameObject(target.ObjectID):nullptr;
    if(!scene||scene->GetInstanceID()!=target.SceneID||!object||object->GetScene()!=scene)return nullptr;
    if(target.ComponentID) {
        for(std::size_t index=0;index<object->GetComponentCount();++index) {
            const auto* component=object->GetComponentAt(index);
            if(component&&component->GetComponentInstanceID()==target.ComponentID)return object;
        }
        return nullptr;
    }
    return object;
}
bool ObjectEditing::CanEdit(const ObjectEditTarget& target,std::string& error) const {
    error.clear();
    if(!OwnerThread()||!_Ready||_Busy||_Continuous.Active||_Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="属性只能在所属主线程的 Runtime 空闲阶段修改。";return false;
    }
    const auto state=_Play->GetState().State;
    const bool edit=state==EditorPlayState::Edit,play=state==EditorPlayState::Playing||state==EditorPlayState::Paused;
    if((!edit&&!play)||(edit&&_Scenes->IsPlaySessionActive())||(play&&!_Scenes->IsPlaySessionActive())) {
        error="正在切换运行模式，暂时不能修改属性。";return false;
    }
    if(_Scenes->HasTransformEdit()) {error="请先完成或取消变换拖拽。";return false;}
    if(!Resolve(target)) {error="属性目标所属世界已切换，或对象、组件已经删除。";return false;}
    return true;
}
bool ObjectEditing::TransientEdit(const ObjectEditTarget& target,const ISceneManager::fSceneEditOperation& apply,
    const ISceneManager::fSceneEditOperation& revert,std::string& error) {
    bool accepted=false;
    try {
        accepted=apply(*Resolve(target),error);
        if(accepted&&!Resolve(target)) {accepted=false;error="属性操作删除或迁移了目标；属性接口不能修改对象结构。";}
        if(accepted)accepted=_Runtime->SynchronizePhysics(error);
    } catch(const std::exception& exception) {accepted=false;error=exception.what();}
      catch(...) {accepted=false;error="运行属性操作抛出异常。";}
    if(accepted)return true;
    if(error.empty())error="Runtime 拒绝修改属性。";
    if(auto* object=Resolve(target)) {
        std::string rollback;
        try {
            if(!revert(*object,rollback))error+=" 恢复原值失败："+rollback;
            else if(!_Runtime->SynchronizePhysics(rollback))
                error+=" 恢复物理状态失败："+rollback;
        } catch(...) {error+=" 恢复原值时发生异常。";}
    } else error+=" 目标已失效，不能恢复已销毁的对象或组件。";
    return false;
}
bool ObjectEditing::EditObject(const ObjectEditTarget& target,const std::string& label,
    const ISceneManager::fSceneEditOperation& apply,const ISceneManager::fSceneEditOperation& revert,std::string& error) {
    if(!CanEdit(target,error))return false;
    if(!apply||!revert){error="属性修改需要有效的正反操作。";return false;}
    EditingScope editing(_Busy);
    const bool playing=_Scenes->IsPlaySessionActive();
    const bool accepted=playing?TransientEdit(target,apply,revert,error):_Scenes->EditObject(target.ObjectID,label,apply,revert,error);
    if(accepted)Notify(target,playing);
    return accepted;
}
bool ObjectEditing::RegisterEditingListener(IObjectEditingListener* listener) {
    if(!OwnerThread()||!listener||_NextListener==std::numeric_limits<std::uint64_t>::max())return false;
    for(const auto& [id,current]:_Listeners)if(current==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void ObjectEditing::UnregisterEditingListener(IObjectEditingListener* listener) {
    if(!OwnerThread())return;
    for(auto current=_Listeners.begin();current!=_Listeners.end();)
        if(current->second==listener)current=_Listeners.erase(current);else ++current;
}
void ObjectEditing::Notify(const ObjectEditTarget& target,bool playing) {
    const ObjectEditEvent event{target,playing};const auto last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last) {
        const auto id=current->first;
        try {current->second->OnObjectPropertiesChanged(event);}catch(...){qWarning("ObjectEditing listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
}
void ObjectEditing::Release(GameSproutEditor::GameSproutModuleApi*) {
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_SelectionSubscribed&&_Selection)_Selection->UnregisterSelectionListener(this);
    _SelectionSubscribed=false;_Selection=nullptr;
    _PlaySubscribed=false;_SceneSubscribed=false;
    std::string ignored;CancelContinuousEdit(ignored);
    delete _Dispatch;_Dispatch=nullptr;_CancelQueued=false;
    _Listeners.clear();_Scenes=nullptr;_Play=nullptr;_Host=nullptr;_Runtime=nullptr;
    _OwnerThread=nullptr;_Ready=false;_Busy=false;_NextListener=0;
}

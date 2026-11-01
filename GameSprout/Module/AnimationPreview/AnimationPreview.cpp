#include "AnimationPreview.h"
#include <QDebug>
#include <limits>

AnimationPreview::~AnimationPreview(){Release(nullptr);}
void AnimationPreview::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api){
    _Runtime=api&&api->_EditorApi?api->_EditorApi->_Runtime:nullptr;
    _Host=api&&api->_EditorApi?api->_EditorApi->GetGameSproutModule<IRuntimeHost>("RuntimeHost"):nullptr;
    _Scenes=api&&api->_EditorApi?api->_EditorApi->GetGameSproutModule<ISceneManager>("SceneManager"):nullptr;
}
bool AnimationPreview::Initialize(GameSproutEditor::GameSproutModuleApi*){return Runtime()&&_Host&&_Scenes;}
bool AnimationPreview::Launch(GameSproutEditor::GameSproutModuleApi*){
    if(!_Host||!_Scenes||!Runtime())return false;
    if(!_SceneSubscribed)_SceneSubscribed=_Scenes->RegisterSceneListener(this);
    if(!_RuntimeSubscribed)_RuntimeSubscribed=Runtime()->RegisterListener(this);
    return _SceneSubscribed&&_RuntimeSubscribed;
}
GameSproutRuntime::IGameSproutRuntime* AnimationPreview::Runtime() const{return _Runtime;}
bool AnimationPreview::Ready(std::string& error) const{
    error.clear();
    if(!Runtime()||!_Scenes||!_Scenes->GetActiveScene()){error="当前没有可预览的场景。";return false;}
    if(_Scenes->HasTransformEdit()){error="请先完成或取消 Scene 中的变换拖拽。";return false;}
    if(Runtime()->IsExecuting()){error="Runtime 正在执行回调，不能重入动画预览。";return false;}
    return true;
}
GameSproutRuntime::IAnimatorComponent* AnimationPreview::PreviewAnimator(uint64_t objectID,bool currentScene) const{
    auto* runtime=Runtime();
    auto* object=runtime&&objectID?runtime->QueryGameObject(objectID):nullptr;
    if(!object||!_Scenes||(currentScene&&object->GetScene()!=_Scenes->GetActiveScene()))return nullptr;
    return object->GetComponent<GameSproutRuntime::IAnimatorComponent>();
}
void AnimationPreview::ClearClip(){
    if(auto* runtime=Runtime())if(!runtime->IsExecuting())if(auto* animator=PreviewAnimator(_ClipObjectID,false)){
        if(animator->GetClipPlaybackState().Active)animator->StopClip();else animator->Pause();
    }
    _ClipObjectID=0;
}
void AnimationPreview::PauseOtherAnimators(uint64_t exceptObjectID){
    std::vector<GameSproutRuntime::IGameObject*> nodes;
    if(_Scenes)if(auto* scene=_Scenes->GetActiveScene())nodes.push_back(scene);
    while(!nodes.empty()){
        auto* object=nodes.back();nodes.pop_back();
        if(object->GetInstanceID()!=exceptObjectID)if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())animator->Pause();
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
}
void AnimationPreview::UpdateClock(){
    if(!_Host)return;
    const auto& clip=_PublishedClip;
    const bool allowed=_Scenes&&_Scenes->GetActiveScene()&&!_Scenes->HasTransformEdit();
    _Host->SetSimulationPlaying(allowed&&(_Playing||(clip.Active&&clip.Playing)));
}
bool AnimationPreview::PlayAnimationClip(uint64_t objectID,const GameSproutRuntime::ResourceReference& clip,
    float seconds,float speed,bool loop,std::string& error){
    if(!Ready(error))return false;
    auto* animator=PreviewAnimator(objectID);
    if(!animator){error="预览目标已失效，或没有 Animator 组件。";return false;}
    // Validate/commit the new pose before ending the old session.
    if(!animator->PlayClip(clip,seconds,speed,loop)){
        error=animator->GetLastError();if(error.empty())error="无法播放所选动画。";return false;
    }
    if(_ClipObjectID!=objectID)ClearClip();
    _ClipObjectID=objectID;_Playing=false;
    PauseOtherAnimators(objectID);PublishState();UpdateClock();_Host->ResetClock();UpdateFrameSubscription();error.clear();return true;
}
GameSproutRuntime::AnimationClipPlaybackState AnimationPreview::GetAnimationClipPreviewState(uint64_t objectID) const{
    return objectID&&objectID==_PublishedObjectID?_PublishedClip:GameSproutRuntime::AnimationClipPlaybackState{};
}
bool AnimationPreview::SetAnimationClipPreviewPlaying(uint64_t objectID,bool playing,std::string& error){
    if(!Ready(error))return false;
    auto* animator=objectID==_ClipObjectID?PreviewAnimator(objectID):nullptr;
    if(!animator||!animator->GetClipPlaybackState().Active){error="没有该对象的直接动画预览。";return false;}
    if(!animator->SetClipPlaybackPlaying(playing)){
        error=animator->GetLastError();if(error.empty())error="无法切换动画预览播放状态。";return false;
    }
    PublishState();UpdateClock();_Host->ResetClock();UpdateFrameSubscription();error.clear();return true;
}
bool AnimationPreview::SeekAnimationClipPreview(uint64_t objectID,float seconds,std::string& error){
    if(!Ready(error))return false;
    auto* animator=objectID==_ClipObjectID?PreviewAnimator(objectID):nullptr;
    if(!animator||!animator->GetClipPlaybackState().Active){error="没有该对象的直接动画预览。";return false;}
    if(!animator->SeekClip(seconds)){
        error=animator->GetLastError();if(error.empty())error="无法定位动画预览时间。";return false;
    }
    PublishState();UpdateClock();_Host->ResetClock();UpdateFrameSubscription();error.clear();return true;
}
void AnimationPreview::StopAnimationClipPreview(uint64_t objectID){
    if(!objectID||objectID!=_ClipObjectID||!Runtime()||Runtime()->IsExecuting())return;
    ClearClip();PublishState();UpdateClock();_Host->ResetClock();UpdateFrameSubscription();
}
void AnimationPreview::SetAnimationPreviewPlaying(bool playing){
    std::string error;if(!Ready(error))return;
    if(playing)ClearClip();
    else if(auto* animator=PreviewAnimator(_ClipObjectID))animator->SetClipPlaybackPlaying(false);
    _Playing=playing;
    std::vector<GameSproutRuntime::IGameObject*> nodes{_Scenes->GetActiveScene()};
    while(!nodes.empty()){
        auto* object=nodes.back();nodes.pop_back();
        if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>()){
            if(playing)animator->Resume();else animator->Pause();
        }
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
    PublishState();UpdateClock();_Host->ResetClock();UpdateFrameSubscription();
}
void AnimationPreview::ResetAnimationPreview(){
    auto* runtime=Runtime();if(!runtime||runtime->IsExecuting())return;
    ClearClip();_Playing=false;_PendingAnimators.clear();_CheckClip=false;
    std::vector<GameSproutRuntime::IGameObject*> nodes;
    if(_Scenes)if(auto* scene=_Scenes->GetActiveScene())nodes.push_back(scene);
    while(!nodes.empty()){
        auto* object=nodes.back();nodes.pop_back();
        if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())animator->Stop();
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
    PublishState();UpdateClock();if(_Host)_Host->ResetClock();UpdateFrameSubscription();NotifyReset();
}
void AnimationPreview::Update(GameSproutEditor::GameSproutModuleApi*) {}
void AnimationPreview::OnActiveSceneChanged(){ResetAnimationPreview();}
void AnimationPreview::OnSceneStateChanged(){UpdateClock();}

bool AnimationPreview::RegisterPreviewListener(IAnimationPreviewListener* listener){
    if(!listener||_NextListener==std::numeric_limits<uint64_t>::max())return false;
    for(const auto& entry:_Listeners)if(entry.second==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void AnimationPreview::UnregisterPreviewListener(IAnimationPreviewListener* listener){
    for(auto current=_Listeners.begin();current!=_Listeners.end();)
        if(current->second==listener)current=_Listeners.erase(current);else ++current;
}
void AnimationPreview::NotifyReset(){
    const uint64_t revision=++_StateRevision,last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last){
        const uint64_t id=current->first;
        try{current->second->OnAnimationPreviewReset();}catch(...){qWarning("Animation preview reset listener threw an exception");}
        if(revision!=_StateRevision)return;
        current=_Listeners.upper_bound(id);
    }
}
GameSproutRuntime::AnimationClipPlaybackState AnimationPreview::CurrentClipState() const {
    // Scene changes are handled by OnActiveSceneChanged. A playing-frame sample
    // resolves only the active Animator, without querying SceneManager each tick.
    auto* animator=PreviewAnimator(_ClipObjectID,false);
    return animator?animator->GetClipPlaybackState():GameSproutRuntime::AnimationClipPlaybackState{};
}
void AnimationPreview::PublishState(bool force){PublishState(CurrentClipState(),force);}
void AnimationPreview::PublishState(const GameSproutRuntime::AnimationClipPlaybackState& clip,bool force){
    const bool sceneChanged=force||_PublishedPlaying!=_Playing;
    const uint64_t oldObjectID=_PublishedObjectID,objectID=_ClipObjectID;
    const bool clipChanged=force||oldObjectID!=objectID||_PublishedClip.Active!=clip.Active
        ||_PublishedClip.Playing!=clip.Playing||_PublishedClip.Clip!=clip.Clip||_PublishedClip.Time!=clip.Time
        ||_PublishedClip.Loop!=clip.Loop||_PublishedClip.Speed!=clip.Speed||_PublishedClip.Duration!=clip.Duration;
    _PublishedPlaying=_Playing;_PublishedObjectID=objectID;_PublishedClip=clip;
    if(!sceneChanged&&!clipChanged)return;
    const uint64_t revision=++_StateRevision,last=_NextListener;
    const bool scenePlaying=_Playing;
    const auto snapshot=clip;
    const GameSproutRuntime::AnimationClipPlaybackState empty;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last){
        const uint64_t id=current->first;
        if(sceneChanged)try{current->second->OnAnimationPreviewStateChanged(scenePlaying);}catch(...){qWarning("Animation preview listener threw an exception");}
        if(revision!=_StateRevision)return;
        current=_Listeners.find(id);
        if(oldObjectID&&oldObjectID!=objectID&&current!=_Listeners.end())
            try{current->second->OnClipPreviewStateChanged(oldObjectID,empty);}catch(...){qWarning("Clip preview listener threw an exception");}
        if(revision!=_StateRevision)return;
        current=_Listeners.find(id);
        if(objectID&&clipChanged&&current!=_Listeners.end())
            try{current->second->OnClipPreviewStateChanged(objectID,snapshot);}catch(...){qWarning("Clip preview listener threw an exception");}
        if(revision!=_StateRevision)return;
        current=_Listeners.upper_bound(id);
    }
}
void AnimationPreview::UpdateFrameSubscription(){
    if(!_Host)return;
    const bool needed=_CheckClip||!_PendingAnimators.empty()||(_ClipObjectID&&_PublishedClip.Active&&_PublishedClip.Playing);
    if(needed&&!_FrameSubscribed)_FrameSubscribed=_Host->RegisterFrameListener(this);
    else if(!needed&&_FrameSubscribed){_Host->UnregisterFrameListener(this);_FrameSubscribed=false;}
}
void AnimationPreview::OnRuntimeFrame(const RuntimeFrameEvent&){
    if(!Runtime())return;
    const auto pending=std::move(_PendingAnimators);_PendingAnimators.clear();
    for(const auto id:pending)if(auto* animator=PreviewAnimator(id)) {
        if(_ClipObjectID&&id!=_ClipObjectID)animator->Pause();
        else if(_Playing)animator->Resume();
    }
    const auto clip=CurrentClipState(); // 每个活跃 Clip 每帧只读取这一份快照。
    const bool clockChanged=_CheckClip||_PublishedClip.Active!=clip.Active||_PublishedClip.Playing!=clip.Playing;
    _CheckClip=false;
    if(!clip.Active)_ClipObjectID=0;
    PublishState(clip);
    if(clockChanged)UpdateClock();
    UpdateFrameSubscription();
}
void AnimationPreview::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event){
    if(event.Type==GameSproutRuntime::SceneNodeEventType::Destroy){
        _PendingAnimators.erase(event.ObjectID);
        if(event.ObjectID==_ClipObjectID)_ClipObjectID=0;
        if(_Scenes){auto* scene=_Scenes->GetActiveScene();if(!scene||event.ObjectID==scene->GetInstanceID())_Playing=false;}
        PublishState();UpdateClock();UpdateFrameSubscription();return;
    }
    if((_Playing||_ClipObjectID)&&event.Type==GameSproutRuntime::SceneNodeEventType::ComponentAdded
        &&event.ComponentType=="GameSproutRuntime.Animator")_PendingAnimators.insert(event.ObjectID);
    if(event.ObjectID==_ClipObjectID&&(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentRemoved
        ||event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged))_CheckClip=true;
    UpdateFrameSubscription();
}
void AnimationPreview::Release(GameSproutEditor::GameSproutModuleApi*){
    _Listeners.clear();
    ResetAnimationPreview();
    if(_FrameSubscribed&&_Host)_Host->UnregisterFrameListener(this);
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_RuntimeSubscribed)if(auto* runtime=Runtime())runtime->UnregisterListener(this);
    _SceneSubscribed=false;_RuntimeSubscribed=false;_FrameSubscribed=false;_CheckClip=false;
    _PendingAnimators.clear();_ClipObjectID=0;_Playing=false;_PublishedObjectID=0;_PublishedPlaying=false;_PublishedClip={};
    _Host=nullptr;_Scenes=nullptr;_Runtime=nullptr;
}

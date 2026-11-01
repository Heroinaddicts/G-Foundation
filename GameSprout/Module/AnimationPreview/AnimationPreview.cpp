#include "AnimationPreview.h"

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
    const auto clip=GetAnimationClipPreviewState(_ClipObjectID);
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
    PauseOtherAnimators(objectID);UpdateClock();_Host->ResetClock();error.clear();return true;
}
GameSproutRuntime::AnimationClipPlaybackState AnimationPreview::GetAnimationClipPreviewState(uint64_t objectID) const{
    if(!objectID||objectID!=_ClipObjectID)return {};
    auto* animator=PreviewAnimator(objectID);
    return animator?animator->GetClipPlaybackState():GameSproutRuntime::AnimationClipPlaybackState{};
}
bool AnimationPreview::SetAnimationClipPreviewPlaying(uint64_t objectID,bool playing,std::string& error){
    if(!Ready(error))return false;
    auto* animator=objectID==_ClipObjectID?PreviewAnimator(objectID):nullptr;
    if(!animator||!animator->GetClipPlaybackState().Active){error="没有该对象的直接动画预览。";return false;}
    if(!animator->SetClipPlaybackPlaying(playing)){
        error=animator->GetLastError();if(error.empty())error="无法切换动画预览播放状态。";return false;
    }
    UpdateClock();_Host->ResetClock();error.clear();return true;
}
bool AnimationPreview::SeekAnimationClipPreview(uint64_t objectID,float seconds,std::string& error){
    if(!Ready(error))return false;
    auto* animator=objectID==_ClipObjectID?PreviewAnimator(objectID):nullptr;
    if(!animator||!animator->GetClipPlaybackState().Active){error="没有该对象的直接动画预览。";return false;}
    if(!animator->SeekClip(seconds)){
        error=animator->GetLastError();if(error.empty())error="无法定位动画预览时间。";return false;
    }
    UpdateClock();_Host->ResetClock();error.clear();return true;
}
void AnimationPreview::StopAnimationClipPreview(uint64_t objectID){
    if(!objectID||objectID!=_ClipObjectID||!Runtime()||Runtime()->IsExecuting())return;
    ClearClip();UpdateClock();_Host->ResetClock();
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
    UpdateClock();_Host->ResetClock();
}
void AnimationPreview::ResetAnimationPreview(){
    auto* runtime=Runtime();if(!runtime||runtime->IsExecuting())return;
    ClearClip();_Playing=false;
    std::vector<GameSproutRuntime::IGameObject*> nodes;
    if(_Scenes)if(auto* scene=_Scenes->GetActiveScene())nodes.push_back(scene);
    while(!nodes.empty()){
        auto* object=nodes.back();nodes.pop_back();
        if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())animator->Stop();
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
    UpdateClock();if(_Host)_Host->ResetClock();
}
void AnimationPreview::Update(GameSproutEditor::GameSproutModuleApi*){
    if(!Runtime())return;
    const auto clip=GetAnimationClipPreviewState(_ClipObjectID);
    if(!clip.Active&&_ClipObjectID)ClearClip();
    if(clip.Active)PauseOtherAnimators(_ClipObjectID);
    UpdateClock();
}
void AnimationPreview::OnActiveSceneChanged(){ResetAnimationPreview();}
void AnimationPreview::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event){
    if(event.Type!=GameSproutRuntime::SceneNodeEventType::Destroy)return;
    if(event.ObjectID==_ClipObjectID)_ClipObjectID=0;
    if(_Scenes){auto* scene=_Scenes->GetActiveScene();if(!scene||event.ObjectID==scene->GetInstanceID())_Playing=false;}
    UpdateClock();
}
void AnimationPreview::Release(GameSproutEditor::GameSproutModuleApi*){
    ResetAnimationPreview();
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_RuntimeSubscribed)if(auto* runtime=Runtime())runtime->UnregisterListener(this);
    _SceneSubscribed=false;_RuntimeSubscribed=false;_ClipObjectID=0;_Playing=false;_Host=nullptr;_Scenes=nullptr;_Runtime=nullptr;
}

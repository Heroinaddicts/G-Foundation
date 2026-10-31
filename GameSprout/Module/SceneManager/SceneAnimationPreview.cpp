#include "SceneManager.h"

GameSproutRuntime::IAnimatorComponent* SceneManager::PreviewAnimator(uint64_t objectID) const {
    auto* scene=GetActiveScene();
    auto* object=scene&&objectID?_Runtime->QueryGameObject(objectID):nullptr;
    return object&&object->GetScene()==scene?object->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
}

void SceneManager::ClearAnimationClipPreview() {
    if(auto* animator=PreviewAnimator(_ClipPreviewObjectID)) {
        if(animator->GetClipPlaybackState().Active)animator->StopClip();
        else animator->Pause();
    }
    _ClipPreviewObjectID=0;
}

void SceneManager::PauseControllerAnimators(uint64_t exceptObjectID) {
    std::vector<GameSproutRuntime::IGameObject*> nodes;
    if(auto* scene=GetActiveScene())nodes.push_back(scene);
    while(!nodes.empty()) {
        auto* object=nodes.back();nodes.pop_back();
        if(object->GetInstanceID()!=exceptObjectID)
            if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())animator->Pause();
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
}

bool SceneManager::PlayAnimationClip(uint64_t objectID,const GameSproutRuntime::ResourceReference& clip,
    float seconds,float speed,bool loop,std::string& error) {
    if(!Ready(error))return false;
    auto* animator=PreviewAnimator(objectID);
    if(!animator){error="预览目标已失效，或没有 Animator 组件。";return false;}
    // Runtime 先验证并提交新 Clip。失败时不停止原对象，也不修改共享预览时钟。
    if(!animator->PlayClip(clip,seconds,speed,loop)) {
        error=animator->GetLastError();
        if(error.empty())error="无法播放所选动画。";
        return false;
    }
    if(_ClipPreviewObjectID!=objectID)ClearAnimationClipPreview();
    _ClipPreviewObjectID=objectID;
    _AnimationPreviewPlaying=false;
    _Timer.restart();
    // 只让选中的 Clip 随 Runtime 时钟前进；不要调用全场景 Resume。
    PauseControllerAnimators(objectID);
    error.clear();return true;
}

GameSproutRuntime::AnimationClipPlaybackState SceneManager::GetAnimationClipPreviewState(uint64_t objectID) const {
    if(!objectID||objectID!=_ClipPreviewObjectID)return {};
    auto* animator=PreviewAnimator(objectID);
    return animator?animator->GetClipPlaybackState():GameSproutRuntime::AnimationClipPlaybackState{};
}

bool SceneManager::SetAnimationClipPreviewPlaying(uint64_t objectID,bool playing,std::string& error) {
    if(!Ready(error))return false;
    auto* animator=objectID==_ClipPreviewObjectID?PreviewAnimator(objectID):nullptr;
    if(!animator||!animator->GetClipPlaybackState().Active){error="没有该对象的直接动画预览。";return false;}
    if(!animator->SetClipPlaybackPlaying(playing)) {
        error=animator->GetLastError();if(error.empty())error="无法切换动画预览播放状态。";return false;
    }
    _Timer.restart();error.clear();return true;
}

bool SceneManager::SeekAnimationClipPreview(uint64_t objectID,float seconds,std::string& error) {
    if(!Ready(error))return false;
    auto* animator=objectID==_ClipPreviewObjectID?PreviewAnimator(objectID):nullptr;
    if(!animator||!animator->GetClipPlaybackState().Active){error="没有该对象的直接动画预览。";return false;}
    if(!animator->SeekClip(seconds)) {
        error=animator->GetLastError();if(error.empty())error="无法定位动画预览时间。";return false;
    }
    _Timer.restart();error.clear();return true;
}

void SceneManager::StopAnimationClipPreview(uint64_t objectID) {
    if(!objectID||objectID!=_ClipPreviewObjectID||!_Runtime||_Runtime->IsExecuting())return;
    ClearAnimationClipPreview();_Timer.restart();
}

void SceneManager::SetAnimationPreviewPlaying(bool playing) {
    std::string error;if(!Ready(error))return;
    if(playing)ClearAnimationClipPreview();
    else if(auto* animator=PreviewAnimator(_ClipPreviewObjectID))animator->SetClipPlaybackPlaying(false);
    _AnimationPreviewPlaying=playing;
    _Timer.restart();
    std::vector<GameSproutRuntime::IGameObject*> nodes{GetActiveScene()};
    while(!nodes.empty()) {
        auto* object=nodes.back();nodes.pop_back();
        if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>()) {
            if(playing)animator->Resume();else animator->Pause();
        }
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
}

void SceneManager::ResetAnimationPreview() {
    if(!_Runtime||_Runtime->IsExecuting()||!GetActiveScene())return;
    ClearAnimationClipPreview();_AnimationPreviewPlaying=false;_Timer.restart();
    std::vector<GameSproutRuntime::IGameObject*> nodes{GetActiveScene()};
    while(!nodes.empty()) {
        auto* object=nodes.back();nodes.pop_back();
        if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())animator->Stop();
        for(std::size_t index=0;index<object->GetChildCount();++index)nodes.push_back(object->GetChild(index));
    }
}

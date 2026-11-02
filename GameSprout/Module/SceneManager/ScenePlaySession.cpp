#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDebug>
#include <exception>
#include <stdexcept>

namespace {
class SwitchingScope {
public:
    explicit SwitchingScope(bool& value):_Value(value) {_Value=true;}
    ~SwitchingScope() {_Value=false;}
private:
    bool& _Value;
};
void DestroyCandidates(GameSproutRuntime::IGameSproutRuntime& runtime,
                       const std::vector<GameSproutRuntime::IScene*>& scenes) noexcept {
    for(auto* scene:scenes) try {runtime.DestroyScene(scene);} catch(...) {}
}
}

void SceneManager::NotifyPlaySession(bool changing,bool active) {
    const auto last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last) {
        const auto id=current->first;
        try {
            if(changing) current->second->OnPlaySessionChanging(active);
            else current->second->OnPlaySessionChanged(active);
        } catch(...) {qWarning("Scene play session listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
}

bool SceneManager::BeginPlaySession(std::string& error) {
    error.clear();
    if(IsDocumentSessionActive()){error="请先关闭当前临时文档，再开始运行。";return false;}
    if(IsPlaySessionActive()) {error="已经存在运行场景会话。";return false;}
    if(!Ready(error)) return false;
    if(Runtime()->IsScriptExecutionEnabled()) {error="捕获场景前必须先停止脚本执行。";return false;}
    SwitchingScope switching(_Switching);
    PlaySession* snapshot=nullptr;
    std::vector<GameSproutRuntime::IScene*> candidates,previous;
    std::vector<SceneDocument::ObjectIDs> candidateIDs;
    bool prepared=false;
    try {
        snapshot=new PlaySession();
        snapshot->Path=_ActivePath;snapshot->BaselineText=_BaselineText;
        snapshot->DiskHash=_DiskHash;snapshot->LastStateText=_LastStateText;
        snapshot->HistoryPosition=_HistoryPosition;
        snapshot->Annotations=_Annotations;
        const auto count=Runtime()->GetSceneCount();
        snapshot->Scenes.reserve(count);previous.reserve(count);candidates.reserve(count);candidateIDs.reserve(count);
        for(std::size_t index=0;index<count;++index) {
            auto* scene=Runtime()->GetScene(index);
            if(!scene) throw std::runtime_error("编辑场景已经失效。");
            previous.push_back(scene);
            PlaySession::SceneSnapshot item;
            if(scene->GetInstanceID()==_ActiveID) {snapshot->ActiveIndex=index;item.IDs=_ObjectIDs;}
            if(!SceneDocument::Serialize(*scene,item.IDs,item.Document,error)) throw std::runtime_error(error);
            if(scene->GetInstanceID()==_ActiveID&&!_Annotations.empty())item.Document["Annotations"]=_Annotations;
            snapshot->Scenes.push_back(std::move(item));
        }
        const auto& author=snapshot->Scenes.at(snapshot->ActiveIndex);
        snapshot->Unsaved=_ActivePath.empty()||AssetArtifactIO::JsonText(author.Document)!=_BaselineText;
        for(const auto& item:snapshot->Scenes) {
            GameSproutRuntime::IScene* candidate=nullptr;SceneDocument::ObjectIDs ids;
            if(!SceneDocument::Instantiate(item.Document,*Runtime(),candidate,ids,error)) throw std::runtime_error(error);
            candidates.push_back(candidate);candidateIDs.push_back(std::move(ids));
        }
        prepared=true;
    } catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="无法准备运行场景，编辑内容保持不变。";}
    if(!prepared) {
        if(error.empty())error="无法准备运行场景，编辑内容保持不变。";
        DestroyCandidates(*Runtime(),candidates);delete snapshot;return false;
    }
    // 所有可能失败的文档读取与节点分配在发布之前完成。旧树仍可用于选择快照。
    NotifyPlaySession(true,true);
    snapshot->History.swap(_History);_HistoryPosition=0;
    _PlaySession=snapshot;
    _ActiveID=candidates[snapshot->ActiveIndex]->GetInstanceID();
    _ObjectIDs=std::move(candidateIDs[snapshot->ActiveIndex]);
    _StatePending=false;
    NotifySceneChanged();
    DestroyCandidates(*Runtime(),previous);
    NotifyPlaySession(false,true);
    NotifyStateChanged();
    return true;
}

bool SceneManager::EndPlaySession(std::string& error) {
    error.clear();
    if(!IsPlaySessionActive()) return true;
    if(!Runtime()||Runtime()->IsExecuting()||_Switching||_ExecutingEdit||_Notifying) {
        error="场景或 Runtime 正在执行其他操作，不能恢复编辑会话。";return false;
    }
    if(Runtime()->IsScriptExecutionEnabled()) {error="请先停止游戏脚本，再恢复编辑场景。";return false;}
    SwitchingScope switching(_Switching);
    std::vector<GameSproutRuntime::IScene*> candidates,previous;
    std::vector<SceneDocument::ObjectIDs> candidateIDs;
    bool prepared=false;
    try {
        const auto count=Runtime()->GetSceneCount();previous.reserve(count);
        for(std::size_t index=0;index<count;++index) previous.push_back(Runtime()->GetScene(index));
        candidates.reserve(_PlaySession->Scenes.size());candidateIDs.reserve(_PlaySession->Scenes.size());
        for(const auto& item:_PlaySession->Scenes) {
            GameSproutRuntime::IScene* candidate=nullptr;SceneDocument::ObjectIDs ids;
            if(!SceneDocument::Instantiate(item.Document,*Runtime(),candidate,ids,error)) throw std::runtime_error(error);
            // 已删除的历史对象仍占用过 FileID，不能在退出后重新分配给其他对象。
            ids.Next=item.IDs.Next;
            candidates.push_back(candidate);candidateIDs.push_back(std::move(ids));
        }
        prepared=true;
    } catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="无法恢复编辑场景，可以保留当前会话后重试停止。";}
    if(!prepared) {
        if(error.empty())error="无法恢复编辑场景，可以保留当前会话后重试停止。";
        DestroyCandidates(*Runtime(),candidates);return false;
    }
    NotifyPlaySession(true,false);
    PlaySession* snapshot=_PlaySession;
    _ActiveID=candidates[snapshot->ActiveIndex]->GetInstanceID();
    _ObjectIDs=std::move(candidateIDs[snapshot->ActiveIndex]);
    _ActivePath.swap(snapshot->Path);_BaselineText.swap(snapshot->BaselineText);
    _DiskHash.swap(snapshot->DiskHash);_LastStateText.swap(snapshot->LastStateText);
    _History.swap(snapshot->History);_HistoryPosition=snapshot->HistoryPosition;
    _Unsaved=snapshot->Unsaved;_StatePending=false;
    _Annotations.swap(snapshot->Annotations);
    _PlaySession=nullptr;
    NotifySceneChanged();
    DestroyCandidates(*Runtime(),previous);
    delete snapshot;
    NotifyPlaySession(false,false);
    NotifyStateChanged();
    return true;
}

#include "SceneManager.h"
#include "SceneSaveSnapshot.h"
#include <QDebug>
#include <algorithm>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>

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
Json::Value ReadPlayDocument(const SceneDirtyState& state) {
    if(!state.Known)throw std::runtime_error("Scene save snapshot is unavailable");
    const auto count=state.CurrentOrder.size();
    std::vector<Json::Value> rows(count);
    const auto readRows=[&](std::size_t first,std::size_t end) {
        Json::CharReaderBuilder builder;
        builder["rejectDupKeys"]=true;
        builder["failIfExtra"]=true;
        struct Parser {
            Json::CharReader* Reader;
            ~Parser(){delete Reader;}
        } parser{builder.newCharReader()};
        std::string error;
        for(auto index=first;index<end;++index) {
            const auto& row=SceneSaveSnapshot::CurrentRow(state,state.CurrentOrder[index]);
            if(!parser.Reader->parse(row.data(),row.data()+row.size(),&rows[index],&error))
                throw std::runtime_error(error.empty()?"Invalid authored scene row":error);
        }
    };
    // Decode independent saved rows in bounded batches. Workers borrow only
    // immutable snapshot data and finish before touching Runtime or publishing.
    const auto batches=std::max(std::size_t{1},std::min({std::size_t{4},count/1024,
        static_cast<std::size_t>(std::thread::hardware_concurrency())}));
    std::vector<std::future<void>> workers;
    workers.reserve(batches-1);
    for(std::size_t batch=1;batch<batches;++batch) {
        const auto first=count*batch/batches,end=count*(batch+1)/batches;
        try { workers.push_back(std::async(std::launch::async,readRows,first,end)); }
        catch(const std::system_error&) { readRows(first,end); }
    }
    if(count)readRows(0,count/batches);
    for(auto& worker:workers)worker.get();
    Json::Value document=state.CurrentRoot;
    auto& objects=document["Objects"];
    objects=Json::Value(Json::arrayValue);
    for(auto& row:rows)objects.append(std::move(row));
    return document;
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
    if(!SynchronizeDirtyState(error))return false;
    const auto authorUnsaved=DirtyFromSnapshot();
    SwitchingScope switching(_Switching);
    PlaySession* snapshot=nullptr;
    std::vector<GameSproutRuntime::IScene*> candidates;
    std::vector<std::uint64_t> authorIDs;
    DocumentContext playing;
    bool prepared=false;
    try {
        snapshot=new PlaySession();
        playing.Path=_ActivePath;
        playing.DiskHash=_DiskHash;
        playing.Annotations=_Annotations;
        playing.Unsaved=authorUnsaved;
        const auto count=Runtime()->GetSceneCount();
        authorIDs.reserve(count);
        candidates.reserve(count);
        for(std::size_t index=0;index<count;++index) {
            auto* scene=Runtime()->GetScene(index);
            if(!scene) throw std::runtime_error("编辑场景已经失效。");
            authorIDs.push_back(scene->GetInstanceID());
            const bool active=scene->GetInstanceID()==_ActiveID;
            Json::Value document;
            if(active) {
                document=ReadPlayDocument(_DirtyState);
                if(_Annotations.empty())document.removeMember("Annotations");
                else document["Annotations"]=_Annotations;
            } else {
                SceneDocument::ObjectIDs ids;
                if(!SceneDocument::Serialize(*scene,ids,document,error))throw std::runtime_error(error);
            }
            // Runtime keeps live tags on the parked objects. The editor also
            // retains a value snapshot for queries that include source references.
            snapshot->SceneTags.insert(document.get("RootTagID",Json::UInt64(0)).asUInt64());
            for(const auto& node:document["Objects"])
                if(node.isMember("TagID"))snapshot->SceneTags.insert(node["TagID"].asUInt64());
            GameSproutRuntime::IScene* candidate=nullptr;
            SceneDocument::ObjectIDs ids;
            if(!SceneDocument::Instantiate(document,*Runtime(),candidate,ids,error))throw std::runtime_error(error);
            candidates.push_back(candidate);
            if(active) {
                playing.ActiveID=candidate->GetInstanceID();
                playing.IDs=std::move(ids);
            }
        }
        if(!playing.ActiveID)throw std::runtime_error("无法找到编辑场景的运行副本。");
        prepared=true;
    } catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="无法准备运行场景，编辑内容保持不变。";}
    if(!prepared) {
        if(error.empty())error="无法准备运行场景，编辑内容保持不变。";
        DestroyCandidates(*Runtime(),candidates);delete snapshot;return false;
    }
    // Resolve selection against the authoring world before it becomes hidden.
    NotifyPlaySession(true,true);
    if(!Runtime()->ParkAuthoringScenes(authorIDs,error)) {
        NotifyPlaySession(false,false);
        DestroyCandidates(*Runtime(),candidates);delete snapshot;return false;
    }
    SwapDocumentContext(snapshot->Author);
    snapshot->Author.Unsaved=authorUnsaved;
    snapshot->Author.StatePending=false;
    SwapDocumentContext(playing);
    _PlaySession=snapshot;
    NotifySceneChanged();
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
    std::vector<GameSproutRuntime::IScene*> previous;
    try {
        const auto count=Runtime()->GetSceneCount();previous.reserve(count);
        for(std::size_t index=0;index<count;++index) previous.push_back(Runtime()->GetScene(index));
    } catch(const std::exception& exception) {error=exception.what();return false;}
      catch(...) {error="无法恢复编辑场景，可以保留当前会话后重试停止。";return false;}
    NotifyPlaySession(true,false);
    if(!Runtime()->RestoreAuthoringScenes(error))return false;
    PlaySession* snapshot=_PlaySession;
    SwapDocumentContext(snapshot->Author);
    _DirtyState.NotifiedRevision=_DirtyState.Revision;_DirtyState.NotifiedDirty=_Unsaved;
    _PlaySession=nullptr;
    NotifySceneChanged();
    DestroyCandidates(*Runtime(),previous);
    delete snapshot;
    NotifyPlaySession(false,false);
    NotifyStateChanged();
    return true;
}

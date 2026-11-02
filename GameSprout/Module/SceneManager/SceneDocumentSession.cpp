#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include "IRuntimeHost.h"
#include <set>
#include <stdexcept>
#include <utility>

namespace {
bool ContentGraph(const Json::Value& document,uint64_t root,std::string& error) {
    if(!root)return true;
    std::map<uint64_t,uint64_t> parents;
    for(const auto& node:document["Objects"])parents.emplace(node["ID"].asUInt64(),node.get("ParentID",Json::UInt64(0)).asUInt64());
    const auto found=parents.find(root);
    if(found==parents.end()||found->second) {error="临时文档的内容根不能删除、替换身份或移动到其他父级。";return false;}
    for(const auto& [id,parent]:parents) {
        uint64_t current=id;std::set<uint64_t> visited;
        while(current!=root) {
            const auto node=parents.find(current);
            if(node==parents.end()||!node->second||!visited.insert(current).second) {
                error="临时文档的所有节点必须位于内容根内部。";return false;
            }
            current=node->second;
        }
    }
    return true;
}
}

void SceneManager::SwapDocumentContext(DocumentContext& context) noexcept {
    std::swap(_ActiveID,context.ActiveID);std::swap(_ObjectIDs,context.IDs);
    _ActivePath.swap(context.Path);_BaselineText.swap(context.BaselineText);
    _DiskHash.swap(context.DiskHash);_LastStateText.swap(context.LastStateText);
    _History.swap(context.History);std::swap(_HistoryPosition,context.HistoryPosition);
    _Annotations.swap(context.Annotations);std::swap(_Unsaved,context.Unsaved);std::swap(_StatePending,context.StatePending);
}
GameSproutRuntime::IScene* SceneManager::GetMainScene() const {
    if(!_DocumentSession||_EditingSuspended)return GetActiveScene();
    return Runtime()?dynamic_cast<GameSproutRuntime::IScene*>(Runtime()->QueryGameObject(_DocumentSession->Main.ActiveID)):nullptr;
}
std::string SceneManager::GetMainScenePath() const {
    return _DocumentSession&&!_EditingSuspended?_DocumentSession->Main.Path:_ActivePath;
}
GameSproutRuntime::IGameObject* SceneManager::GetDocumentContentRoot() const {
    return IsTemporaryDocumentContext()&&_DocumentSession->ContentRootFileID
        ?QueryObjectByFileID(_DocumentSession->ContentRootFileID):GetActiveScene();
}
bool SceneManager::ValidateDocumentContent(const Json::Value& document,std::string& error) const {
    return !IsTemporaryDocumentContext()||ContentGraph(document,_DocumentSession->ContentRootFileID,error);
}
bool SceneManager::BeginDocumentSession(const SceneDocumentSessionRequest& request,std::string& error) {
    if(_Host&&_Host->IsFrameActive()){error="请在当前宿主帧结束后打开临时文档。";return false;}
    if(IsDocumentSessionActive()){error="请先关闭当前临时文档。";return false;}
    if(!Ready(error))return false;
    if(Runtime()->IsScriptExecutionEnabled()){error="临时文档不能在游戏脚本执行中打开。";return false;}
    if(request.DocumentJson.size()>32*1024*1024){error="临时文档超过32 MiB。";return false;}
    Json::Value document;
    if(!AssetArtifactIO::Parse(request.DocumentJson,document)||!SceneDocument::Validate(document,error)||
       !ContentGraph(document,request.ContentRootFileID,error)) {
        if(error.empty())error="临时文档无效。";return false;
    }
    Json::Value previous;std::string previousText;
    if(!Capture(previous,previousText,error))return false;
    GameSproutRuntime::IScene* candidate=nullptr;SceneDocument::ObjectIDs ids;
    DocumentSession* session=nullptr;
    _Switching=true;
    try {
        if(!SceneDocument::Instantiate(document,*Runtime(),candidate,ids,error)) {_Switching=false;return false;}
        if(!request.Name.empty())candidate->SetName(request.Name);
        session=new DocumentSession();session->ContentRootFileID=request.ContentRootFileID;
        session->MainSimulationEnabled=GetActiveScene()->IsSimulationEnabled();
        // Prepare all allocations before publishing either authoring context.
        DocumentContext temporary;temporary.ActiveID=candidate->GetInstanceID();temporary.IDs=std::move(ids);
        temporary.Annotations=document.isMember("Annotations")?document["Annotations"]:Json::Value(Json::objectValue);
        Json::Value normalized;
        if(!SceneDocument::Serialize(*candidate,temporary.IDs,normalized,error))throw std::runtime_error(error);
        if(!temporary.Annotations.empty())normalized["Annotations"]=temporary.Annotations;
        temporary.BaselineText=AssetArtifactIO::JsonText(normalized);temporary.LastStateText=temporary.BaselineText;temporary.Unsaved=false;
        SwapDocumentContext(session->Main);SwapDocumentContext(temporary);_DocumentSession=session;
        if(auto* main=GetMainScene())main->SetSimulationEnabled(false);
        _TransformEdit={};NotifySceneChanged();NotifyStateChanged();_Switching=false;return true;
    } catch(const std::exception& exception){error=exception.what();}
    catch(...){error="无法创建临时文档，主场景保持不变。";}
    if(candidate)Runtime()->DestroyScene(candidate);delete session;_Switching=false;return false;
}
bool SceneManager::EndDocumentSession(std::string& error) {
    error.clear();if(!_DocumentSession)return true;
    if(_EditingSuspended||!Runtime()||Runtime()->IsExecuting()||(_Host&&_Host->IsFrameActive())||_Switching||_ExecutingEdit||_Notifying||_TransformEdit.Active) {
        error="请先结束当前回调或编辑操作，再关闭临时文档。";return false;
    }
    auto* main=GetMainScene();if(!main){error="挂起的主场景已失效。";return false;}
    _Switching=true;auto* temporary=GetActiveScene();auto* session=_DocumentSession;
    SwapDocumentContext(session->Main);_DocumentSession=nullptr;
    main->SetSimulationEnabled(session->MainSimulationEnabled);
    NotifySceneChanged();NotifyStateChanged();
    if(temporary&&temporary!=main)Runtime()->DestroyScene(temporary);
    delete session;_Switching=false;return true;
}
bool SceneManager::MarkDocumentSessionSaved(std::string& error) {
    if(!IsTemporaryDocumentContext()){error="当前没有可保存的临时文档。";return false;}
    if(!Ready(error))return false;
    Json::Value document;std::string text;if(!Capture(document,text,error))return false;
    std::string state=text;_BaselineText.swap(text);_LastStateText.swap(state);_Unsaved=false;_StatePending=false;
    NotifyStateChanged();return true;
}
bool SceneManager::EditSuspendedDocument(const std::function<bool(std::string&)>& operation,std::string& error) {
    if(!_DocumentSession||_EditingSuspended||!operation){error="没有可编辑的挂起文档，或正在执行后台文档操作。";return false;}
    if(!Ready(error))return false;
    if(_Host&&_Host->IsFrameActive()){error="请在当前宿主帧结束后编辑挂起文档。";return false;}
    _EditingSuspended=true;SwapDocumentContext(_DocumentSession->Main);
    bool success=false;
    try {success=operation(error);}
    catch(const std::exception& exception){error=exception.what();}
    catch(...){error="挂起文档操作发生异常。";}
    SwapDocumentContext(_DocumentSession->Main);_EditingSuspended=false;
    return success;
}

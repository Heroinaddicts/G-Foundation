#include "SceneManager.h"
#include "SceneAnimationDocument.h"
#include "ScenePhysicsDocument.h"
#include "PrefabDocument.h"
#include "../Shared/AssetArtifactIO.h"
#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
class DocumentEditingScope {
public:
    explicit DocumentEditingScope(bool& flag):_Flag(flag){_Flag=true;}
    ~DocumentEditingScope(){_Flag=false;}
private: bool& _Flag;
};
using NodeMap=std::map<uint64_t,Json::Value>;
NodeMap Nodes(const Json::Value& document) {
    NodeMap result;
    for(const auto& node:document["Objects"])result.emplace(node["ID"].asUInt64(),node);
    return result;
}
GameSproutRuntime::IComponent* FindComponent(GameSproutRuntime::IGameObject& object,const Json::Value& data,const SceneDocument::ObjectIDs& ids) {
    const std::string type=data["Type"].asString();
    for(std::size_t i=0;i<object.GetComponentCount();++i) {
        auto* component=object.GetComponentAt(i);
        if(!component||!component->GetTypeName()||type!=component->GetTypeName())continue;
        if(data["ID"].isUInt64()&&type!="GameSproutRuntime.Transform") {
            const auto identity=ids.FindComponentFileID(component);
            if(identity!=data["ID"].asUInt64())continue;
        }
        if(type=="GameSproutRuntime.ScriptComponent") {
            auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component);
            if(!script||script->GetScriptInstanceID()!=data["InstanceID"].asUInt64())continue;
        }
        return component;
    }
    return nullptr;
}
void References(const Json::Value& value,std::set<uint64_t>& output) {
    if(value.isArray()) {for(const auto& item:value)References(item,output);}
    else if(value.isObject())for(const auto& key:value.getMemberNames()) {
        if((key=="ObjectFileID"||key=="ComponentFileID"||key=="AnimatorObjectFileID")&&value[key].isUInt64())output.insert(value[key].asUInt64());
        else References(value[key],output);
    }
}
bool ParseAnnotations(const std::string& text,Json::Value& output,std::string& error) {
    if(!AssetArtifactIO::Parse(text,output)||!output.isObject()) {
        error="场景 annotations 必须是有效 JSON object。";return false;
    }
    return true;
}
uint64_t Allocate(uint64_t& next) {
    if(!next)throw std::runtime_error("场景文档 FileID 已耗尽。");
    const auto result=next;next=result==std::numeric_limits<uint64_t>::max()?0:result+1;return result;
}
bool MapSubtree(Json::Value& subtree,bool remap,uint64_t& next,uint64_t& rootFile,
    std::map<uint64_t,uint64_t>& mapping,std::string& error) {
    const auto mapID=[&](uint64_t id) {
        mapping.emplace(id,remap?Allocate(next):id);
        if(!remap&&next&&id>=next)next=id==std::numeric_limits<uint64_t>::max()?0:id+1;
    };
    for(const auto& node:subtree["Objects"]) {
        mapID(node["ID"].asUInt64());
        for(const auto& component:node["Components"])if(component["ID"].isUInt64())
            mapID(component["ID"].asUInt64());
    }
    if(remap) {
        // References outside the inserted subtree keep their scene identity.
        std::set<uint64_t> references;References(subtree["Objects"],references);mapping.emplace(0,0);
        for(const auto id:references)mapping.try_emplace(id,id);
        if(!PrefabDocument::Remap(subtree,mapping,error))return false;
        rootFile=mapping.at(rootFile);
    }
    subtree["NextObjectID"]=Json::UInt64(next);
    return true;
}
Json::Value Subtree(const Json::Value& document,uint64_t root) {
    const auto nodes=Nodes(document);std::set<uint64_t> selected{root};
    bool added=true;
    while(added) {added=false;for(const auto& entry:nodes)if(selected.count(entry.second["ParentID"].asUInt64())&&selected.insert(entry.first).second)added=true;}
    Json::Value result(Json::objectValue);result["Version"]=Json::UInt(GameSproutRuntime::SceneDocumentVersion);result["Name"]="Object document";
    result["NextObjectID"]=document["NextObjectID"];
    result["MainCameraID"]=Json::UInt64(selected.count(document["MainCameraID"].asUInt64())?document["MainCameraID"].asUInt64():0);
    result["Objects"]=Json::Value(Json::arrayValue);
    for(const auto& node:document["Objects"])if(selected.count(node["ID"].asUInt64())) {
        auto copy=node;if(copy["ID"].asUInt64()==root)copy["ParentID"]=Json::UInt64(0);result["Objects"].append(std::move(copy));
    }
    return result;
}
void FillResult(const Json::Value& subtree,const std::map<uint64_t,uint64_t>& sourceToScene,
                const SceneDocument::ObjectIDs& ids,SceneSubtreeDocument& output) {
    output.DocumentJson=AssetArtifactIO::JsonText(subtree);std::set<uint64_t> local,external;
    std::map<uint64_t,uint64_t> sceneToSource;
    for(const auto& pair:sourceToScene)sceneToSource.emplace(pair.second,pair.first);
    std::map<uint64_t,uint64_t> fileToRuntime;for(const auto& pair:ids.RuntimeToFile)fileToRuntime.emplace(pair.second,pair.first);
    for(const auto& node:subtree["Objects"]) {
        const auto file=node["ID"].asUInt64();local.insert(file);
        const auto runtime=fileToRuntime.find(file);const auto runtimeID=runtime==fileToRuntime.end()?0:runtime->second;
        output.ObjectIDs.push_back({sceneToSource.count(file)?sceneToSource.at(file):file,file,runtimeID,false});
        if(!node["ParentID"].asUInt64())output.RootObjectID=runtimeID;
        for(const auto& component:node["Components"])if(component["ID"].isUInt64()) {
            const auto componentID=component["ID"].asUInt64();
            local.insert(componentID);
            output.ObjectIDs.push_back({sceneToSource.count(componentID)?sceneToSource.at(componentID):componentID,componentID,runtimeID,true});
        }
        References(node["Components"],external);
    }
    for(const auto id:external)if(!local.count(id))output.ExternalObjectFileIDs.push_back(id);
}
}

std::string SceneManager::GetSceneAnnotationsJson() const {return AssetArtifactIO::JsonText(_Annotations);}
bool SceneManager::CaptureSubtree(GameSproutRuntime::IGameObject& root,Json::Value& output,std::string& error) const {
    if(!SynchronizeDirtyState(error))return false;
    Json::Value subtree(Json::objectValue);
    subtree["Version"]=Json::UInt(GameSproutRuntime::SceneDocumentVersion);
    subtree["Name"]="Object document";subtree["NextObjectID"]=Json::UInt64(_ObjectIDs.Next);
    subtree["MainCameraID"]=Json::UInt64(0);subtree["Objects"]=Json::Value(Json::arrayValue);
    const auto* mainCamera=GetActiveScene()->GetMainCamera();
    std::vector<GameSproutRuntime::IGameObject*> pending{&root};
    while(!pending.empty()) {
        auto* node=pending.back();pending.pop_back();
        Json::Value properties;
        if(!SceneDocument::SerializeProperties(*node,_ObjectIDs,properties,error))return false;
        if(node==&root)properties["ParentID"]=Json::UInt64(0);
        if(mainCamera&&mainCamera->GetGameObject()==node)subtree["MainCameraID"]=properties["ID"];
        subtree["Objects"].append(std::move(properties));
        for(std::size_t i=node->GetChildCount();i>0;--i)pending.push_back(node->GetChild(i-1));
    }
    if(!SceneDocument::ValidateSubtree(subtree,error))return false;
    output.swap(subtree);return true;
}

bool SceneManager::CaptureObjectDocument(uint64_t objectID,SceneSubtreeDocument& output,std::string& error) const {
    error.clear();auto* object=Runtime()?Runtime()->QueryGameObject(objectID):nullptr;
    if(!object||object==GetActiveScene()||object->GetScene()!=GetActiveScene()) {error="子树文档需要当前场景中的普通节点。";return false;}
    if(Runtime()->IsExecuting()||IsPlaySessionActive()||_ExecutingEdit||_PropertyPreview) {error="场景事务、属性预览或运行期间不能捕获作者子树文档。";return false;}
    try {
        Json::Value subtree;
        if(!CaptureSubtree(*object,subtree,error))return false;
        SceneDocument::ObjectIDs identities;
        for(const auto& node:subtree["Objects"]) {
            const auto file=node["ID"].asUInt64();
            if(auto* live=QueryObjectByFileID(file))identities.RuntimeToFile.emplace(live->GetInstanceID(),file);
        }
        SceneSubtreeDocument result;FillResult(subtree,{},identities,result);output=std::move(result);return true;
    } catch(const std::exception& exception){error=exception.what();return false;}
}

bool SceneManager::EditObjectDocument(const SceneDocumentEditRequest& request,SceneSubtreeDocument& output,std::string& error) {
    if(!Ready(error))return false;
    DocumentEditingScope editing(_ExecutingEdit);
    try {
        Json::Value subtree;std::map<uint64_t,uint64_t> mapping;uint64_t rootFile=0;
        if(!request.DocumentJson.empty()) {
            if(!AssetArtifactIO::Parse(request.DocumentJson,subtree)||!SceneDocument::ValidateSubtree(subtree,error)) {
                if(error.empty())error="子树文档 JSON 无效。";return false;
            }
            if(subtree["Objects"].empty()||subtree.isMember("RootComponents")) {error="子树文档必须只包含一个普通节点根。";return false;}
            for(const auto& node:subtree["Objects"])if(!node["ParentID"].asUInt64()) {
                if(rootFile) {error="子树文档只能有一个根。";return false;}rootFile=node["ID"].asUInt64();
            }
            if(!rootFile) {error="子树文档没有根节点。";return false;}
            if(!request.RootObjectID&&request.RemapDocumentIDs&&request.AnnotationsJson.empty()&&!request.BuildAnnotations) {
                std::set<uint64_t> references;
                References(subtree["Objects"],references);
                references.erase(0);
                if(references.empty())return InsertObjectDocument(request,subtree,rootFile,output,error);
            }
        }
        Edit edit;edit.Type=Edit::Kind::Document;edit.Label=request.Label;
        if(!Capture(edit.DocumentBefore,error))return false;
        edit.DocumentAfter=edit.DocumentBefore;edit.AnnotationsBefore=_Annotations;edit.AnnotationsAfter=_Annotations;
        if(rootFile) {
            auto* scene=GetActiveScene();auto* parent=request.ParentObjectID?Runtime()->QueryGameObject(request.ParentObjectID):GetDocumentContentRoot();
            auto* original=request.RootObjectID?Runtime()->QueryGameObject(request.RootObjectID):nullptr;
            if(request.RootObjectID&&(!original||original==scene||original->GetScene()!=scene)) {error="替换目标不属于当前场景。";return false;}
            if(original&&!request.ParentObjectID)parent=original->Parent();
            if(!parent||parent->GetScene()!=scene) {error="子树父级不属于当前场景。";return false;}
            const auto parentFile=parent==scene?0:_ObjectIDs.RuntimeToFile.at(parent->GetInstanceID());
            Json::Value oldSubtree;std::set<uint64_t> replaced;
            if(original) {
                const auto originalFile=_ObjectIDs.RuntimeToFile.at(original->GetInstanceID());oldSubtree=Subtree(edit.DocumentBefore,originalFile);
                for(const auto& node:oldSubtree["Objects"])replaced.insert(node["ID"].asUInt64());
                if(replaced.count(parentFile)) {error="不能把子树放到自身或后代中。";return false;}
                if(!request.RemapDocumentIDs&&rootFile!=originalFile) {error="替换文档必须保留根节点 FileID。";return false;}
            }
            auto next=_ObjectIDs.Next;
            if(!MapSubtree(subtree,request.RemapDocumentIDs,next,rootFile,mapping,error))return false;
            edit.DocumentAfter["NextObjectID"]=Json::UInt64(next);
            Json::Value ordered(Json::arrayValue);bool inserted=false;
            for(const auto& node:edit.DocumentBefore["Objects"]) {
                if(replaced.count(node["ID"].asUInt64())) {
                    if(!inserted) {for(auto copy:subtree["Objects"]) {if(copy["ID"].asUInt64()==rootFile)copy["ParentID"]=Json::UInt64(parentFile);ordered.append(std::move(copy));}inserted=true;}
                } else ordered.append(node);
            }
            if(!inserted)for(auto copy:subtree["Objects"]) {if(copy["ID"].asUInt64()==rootFile)copy["ParentID"]=Json::UInt64(parentFile);ordered.append(std::move(copy));}
            edit.DocumentAfter["Objects"].swap(ordered);
            const auto camera=edit.DocumentAfter["MainCameraID"].asUInt64();
            if(replaced.count(camera)&&!Nodes(edit.DocumentAfter).count(camera))edit.DocumentAfter["MainCameraID"]=Json::UInt64(0);
            // Existing scene camera selection takes precedence over a captured subtree's camera marker.
            if(!SceneDocument::Validate(edit.DocumentAfter,error)||!ValidateDocumentContent(edit.DocumentAfter,error))return false;
        }
        SceneSubtreeDocument planned;
        if(rootFile)FillResult(subtree,mapping,_ObjectIDs,planned);
        std::string annotations=request.AnnotationsJson;
        if(request.BuildAnnotations) {
            if(!request.BuildAnnotations(planned,GetSceneAnnotationsJson(),annotations,error))return false;
        }
        if(!annotations.empty()&&!ParseAnnotations(annotations,edit.AnnotationsAfter,error))return false;
        if(edit.AnnotationsAfter.empty())edit.DocumentAfter.removeMember("Annotations");else edit.DocumentAfter["Annotations"]=edit.AnnotationsAfter;
        _History.reserve(_HistoryPosition+1);
        if(!RunDocumentEdit(edit,true,error)) {RefreshState(true);return false;}
        Json::Value committed;
        if(!Capture(committed,error)) {
            std::string rollback;ApplyDocument(edit.DocumentAfter,edit.DocumentBefore,rollback);_Annotations=edit.AnnotationsBefore;
            if(!rollback.empty())error+=" 恢复原文档失败："+rollback;
            RefreshState(true);return false;
        }
        // Optional defaults, float normalization, and JSON's signed/unsigned
        // parser representation must not make a fresh history record stale.
        edit.DocumentAfter.swap(committed);
        if(rootFile) {SceneSubtreeDocument result;FillResult(Subtree(edit.DocumentAfter,rootFile),mapping,_ObjectIDs,result);output=std::move(result);}else output={};
        if(edit.DocumentBefore!=edit.DocumentAfter) {_History.resize(_HistoryPosition);_History.push_back(std::move(edit));++_HistoryPosition;}
        RefreshState(true);return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="场景文档事务失败。";}
    RefreshState(true);return false;
}

bool SceneManager::InsertObjectDocument(const SceneDocumentEditRequest& request,Json::Value& subtree,
    uint64_t rootFile,SceneSubtreeDocument& output,std::string& error) {
    auto* scene=GetActiveScene();
    auto* content=GetDocumentContentRoot();
    auto* parent=request.ParentObjectID?Runtime()->QueryGameObject(request.ParentObjectID):content;
    if(!parent||parent->GetScene()!=scene) {error="子树父级不属于当前场景。";return false;}
    auto* ancestor=parent;
    while(ancestor&&ancestor!=content)ancestor=ancestor->Parent();
    if(!ancestor||!content) {error="子树父级不属于当前编辑文档。";return false;}
    if(!SynchronizeDirtyState(error))return false;

    // Insertion with no object references or annotation changes can be
    // recorded as a creation. Keep existing scene nodes out of the snapshot.
    std::map<uint64_t,uint64_t> mapping;
    auto next=_ObjectIDs.Next;
    if(!MapSubtree(subtree,true,next,rootFile,mapping,error))return false;
    // Inserting an object template preserves the scene's camera choice.
    subtree["MainCameraID"]=Json::UInt64(0);
    Edit edit;
    edit.Type=Edit::Kind::Creation;
    edit.Label=request.Label;
    edit.FileID=rootFile;
    edit.ParentFileID=parent==scene?0:_ObjectIDs.RuntimeToFile.at(parent->GetInstanceID());
    edit.SiblingIndex=parent->GetChildCount();
    _History.reserve(_HistoryPosition+1);

    auto* runtime=Runtime();
    GameSproutRuntime::IGameObject* inserted=nullptr;
    const auto previousNext=_ObjectIDs.Next;
    _ObjectIDs.Next=next;
    if(!SceneDocument::RestoreSubtree(subtree,*runtime,*scene,*parent,edit.SiblingIndex,_ObjectIDs,inserted,error)) {
        _ObjectIDs.Next=previousNext;
        RefreshState(true);
        return false;
    }
    SceneSubtreeDocument result;
    bool captured=false;
    try {
        // Capture normalized component defaults and assigned component IDs for
        // Redo, and publish only the identities that belong to this insertion.
        if(!CaptureSubtree(*inserted,edit.Subtree,error))throw std::runtime_error(error);
        SceneDocument::ObjectIDs identities;
        for(const auto& node:edit.Subtree["Objects"]) {
            const auto file=node["ID"].asUInt64();
            if(auto* object=QueryObjectByFileID(file))identities.RuntimeToFile.emplace(object->GetInstanceID(),file);
        }
        FillResult(edit.Subtree,mapping,identities,result);
        edit.Subtree.removeMember("NextObjectID");
        captured=true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="无法记录新建子树。";}
    if(captured) {
        _History.resize(_HistoryPosition);
        _History.push_back(std::move(edit));
        ++_HistoryPosition;
        output=std::move(result);
    } else if(!scene->DestroyGameObject(inserted))error+=" 无法清理新建子树。";
    RefreshState(true);
    return captured;
}

bool SceneManager::RunDocumentEdit(const Edit& edit,bool forward,std::string& error) {
    const auto& expected=forward?edit.DocumentBefore:edit.DocumentAfter;
    const auto& desired=forward?edit.DocumentAfter:edit.DocumentBefore;
    const auto& expectedAnnotations=forward?edit.AnnotationsBefore:edit.AnnotationsAfter;
    const auto& desiredAnnotations=forward?edit.AnnotationsAfter:edit.AnnotationsBefore;
    Json::Value current;if(!Capture(current,error))return false;
    if(_Annotations!=expectedAnnotations) {error="场景 annotations 已在事务之外变化，无法撤销/重做。";return false;}
    const auto oldNodes=Nodes(expected),newNodes=Nodes(desired),currentNodes=Nodes(current);
    std::set<uint64_t> changed;
    for(const auto& entry:oldNodes)if(!newNodes.count(entry.first)||newNodes.at(entry.first)!=entry.second)changed.insert(entry.first);
    for(const auto& entry:newNodes)if(!oldNodes.count(entry.first)||oldNodes.at(entry.first)!=entry.second)changed.insert(entry.first);
    for(const auto id:changed) {
        const auto was=oldNodes.find(id),now=currentNodes.find(id);
        if((was==oldNodes.end())!=(now==currentNodes.end())||(was!=oldNodes.end()&&was->second!=now->second)) {
            error="子树文档 FileID "+std::to_string(id)+" 已在事务之外变化，无法撤销/重做。";
            return false;
        }
    }
    Json::Value target=current,ordered(Json::arrayValue);
    std::vector<Json::Value> orderedNodes;
    std::set<uint64_t> emitted;
    for(const auto& node:desired["Objects"]) {
        const auto id=node["ID"].asUInt64();
        if(changed.count(id)) {orderedNodes.push_back(node);emitted.insert(id);}
        else if(currentNodes.count(id)) {orderedNodes.push_back(currentNodes.at(id));emitted.insert(id);}
    }
    // Preserve later, unrelated nodes and their position relative to surviving
    // siblings while restoring the edited document's own sibling ordering.
    const auto& currentObjects=current["Objects"];
    for(Json::ArrayIndex index=0;index<currentObjects.size();++index) {
        const auto& node=currentObjects[index];const auto id=node["ID"].asUInt64();
        if(changed.count(id)||emitted.count(id))continue;
        auto insertion=orderedNodes.end();
        for(Json::ArrayIndex next=index+1;next<currentObjects.size();++next) {
            const auto anchor=currentObjects[next]["ID"].asUInt64();
            insertion=std::find_if(orderedNodes.begin(),orderedNodes.end(),[&](const Json::Value& item){return item["ID"].asUInt64()==anchor;});
            if(insertion!=orderedNodes.end())break;
        }
        orderedNodes.insert(insertion,node);emitted.insert(id);
    }
    for(auto& node:orderedNodes)ordered.append(std::move(node));
    target["Objects"].swap(ordered);
    for(const char* key:{"MainCameraID","RootMainCamera"})if(expected[key]!=desired[key]) {
        if(current[key]!=expected[key]) {error="场景主相机已被其他操作改变。";return false;}
        if(desired.isMember(key))target[key]=desired[key];else target.removeMember(key);
    }
    target["NextObjectID"]=Json::UInt64(_ObjectIDs.Next);
    uint64_t maximum=0;
    for(const auto& node:target["Objects"]) {
        maximum=std::max(maximum,node["ID"].asUInt64());
        for(const auto& component:node["Components"])if(component["ID"].isUInt64())maximum=std::max(maximum,component["ID"].asUInt64());
    }
    if(_ObjectIDs.Next&&_ObjectIDs.Next<=maximum)target["NextObjectID"]=Json::UInt64(maximum==std::numeric_limits<uint64_t>::max()?0:maximum+1);
    if(desiredAnnotations.empty())target.removeMember("Annotations");else target["Annotations"]=desiredAnnotations;
    if(!SceneDocument::Validate(target,error)||!ValidateDocumentContent(target,error))return false;
    Json::Value nextAnnotations=desiredAnnotations;
    if(!ApplyDocument(current,target,error))return false;
    _Annotations.swap(nextAnnotations);return true;
}

bool SceneManager::ApplyDocument(const Json::Value& before,const Json::Value& after,std::string& error) {
    auto* scene=GetActiveScene();auto* runtime=Runtime();if(!scene||!runtime) {error="当前场景已经失效。";return false;}
    const auto& tags=runtime->GetProjectSettings().Tags;
    const auto defined=[&](std::uint64_t id){return std::any_of(tags.begin(),tags.end(),[&](const auto& tag){return tag.ID==id;});};
    if(!defined(after.get("RootTagID",Json::UInt64(0)).asUInt64())) {error="场景文档引用了未定义的 TagID。";return false;}
    for(const auto& node:after["Objects"])if(!defined(node.get("TagID",Json::UInt64(0)).asUInt64())) {
        error="节点文档引用了未定义的 TagID。";return false;
    }
    const auto sceneID=scene->GetInstanceID();
    const auto previousNext=_ObjectIDs.Next;
    bool constructionOpen=false;
    const auto patch=[&](const Json::Value& document,std::string& reason)->bool {
        const auto targetNodes=Nodes(document);
        std::map<uint64_t,GameSproutRuntime::IGameObject*> objects;
        for(const auto& pair:_ObjectIDs.RuntimeToFile) {
            auto* object=runtime->QueryGameObject(pair.first);
            if(object&&object->GetScene()==scene)objects.emplace(pair.second,object);
        }
        for(const auto& node:document["Objects"]) {
            const auto file=node["ID"].asUInt64();if(objects.count(file))continue;
            auto* object=scene->CreateGameObject(node["Name"].asString());
            if(!object) {reason="Runtime 拒绝创建文档节点。";return false;}
            objects.emplace(file,object);_ObjectIDs.RuntimeToFile.emplace(object->GetInstanceID(),file);
        }
        // Break old parent chains before applying a valid new hierarchy. A child may
        // become an ancestor in the new document, so direct reparenting can transiently cycle.
        for(const auto& node:document["Objects"]) {
            auto* object=objects.at(node["ID"].asUInt64());const auto parentFile=node["ParentID"].asUInt64();
            auto* parent=parentFile?objects.at(parentFile):scene;
            if(object->Parent()!=parent&&object->Parent()!=scene&&!object->SetParent(scene,false)) {reason="Runtime 拒绝暂存节点父级。";return false;}
        }
        for(const auto& node:document["Objects"]) {
            auto* object=objects.at(node["ID"].asUInt64());const auto parentFile=node["ParentID"].asUInt64();
            auto* parent=parentFile?objects.at(parentFile):scene;
            if(object->Parent()!=parent&&!object->SetParent(parent,false)) {reason="Runtime 拒绝文档父级关系。";return false;}
        }
        std::vector<GameSproutRuntime::SceneObjectIdentity> identities{{sceneID,0}};
        for(const auto& pair:objects)if(targetNodes.count(pair.first))identities.push_back({pair.second->GetInstanceID(),pair.first});
        if(!runtime->BindSceneObjectIdentities(scene,identities,reason))return false;
        for(const auto& node:document["Objects"]) {
            auto* object=objects.at(node["ID"].asUInt64());
            if(!object->SetLayer(static_cast<std::uint8_t>(node.get("Layer",0).asUInt()))||
               !object->SetTagID(node.get("TagID",Json::UInt64(0)).asUInt64())) {
                reason="Runtime 拒绝节点 Layer 或未定义的 TagID。";return false;
            }
            if(!object->SetStaticFlags(static_cast<GameSproutRuntime::StaticFlags>(node.get("StaticFlags",0).asUInt()))) {
                reason="Runtime 拒绝节点静态标志。";return false;
            }
            object->SetName(node["Name"].asString());object->SetActive(node.get("Active",true).asBool());
            std::vector<GameSproutRuntime::IComponent*> removed;
            for(std::size_t i=0;i<object->GetComponentCount();++i) {
                auto* component=object->GetComponentAt(i);bool keep=false;
                for(const auto& data:node["Components"])if(FindComponent(*object,data,_ObjectIDs)==component) {
                    const auto existingID=_ObjectIDs.FindComponentFileID(component);
                    keep=!data["ID"].isUInt64()||!existingID||existingID==data["ID"].asUInt64()||data["Type"]=="GameSproutRuntime.Transform";
                    if(auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component))
                        keep=keep&&script->GetScriptTypeID()==data["ScriptTypeID"].asString()&&script->GetScriptSource().Guid==data["Source"]["Guid"].asString()&&script->GetScriptSource().LocalID==data["Source"]["LocalID"].asUInt64();
                    break;
                }
                if(!keep&&std::string(component->GetTypeName())!="GameSproutRuntime.Transform")removed.push_back(component);
            }
            for(auto* component:removed) {
                _ObjectIDs.ForgetComponentFileID(component);
                if(!object->RemoveComponentInstance(component->GetComponentInstanceID())) {reason="Runtime 拒绝精确移除组件。";return false;}
            }
            for(Json::ArrayIndex index=0;index<node["Components"].size();++index) {
                const auto& data=node["Components"][index];auto* component=FindComponent(*object,data,_ObjectIDs);
                bool same=false;if(component) {
                    try {same=SceneDocument::SerializeComponent(*component,_ObjectIDs)==data;}catch(...) {}
                }
                if(!same) {
                    if(component&&ScenePhysicsDocument::Handles(data["Type"].asString()))ScenePhysicsDocument::Apply(data,*component);
                    else {
                        std::set<uint64_t> previous;
                        for(std::size_t i=0;i<object->GetComponentCount();++i)previous.insert(object->GetComponentAt(i)->GetComponentInstanceID());
                        if(!SceneDocument::RestoreComponent(data,*object,*runtime,reason))return false;
                        component=FindComponent(*object,data,_ObjectIDs);
                        if(!component)for(std::size_t i=0;i<object->GetComponentCount();++i) {
                            auto* candidate=object->GetComponentAt(i);
                            if(!previous.count(candidate->GetComponentInstanceID())&&data["Type"]==candidate->GetTypeName()){component=candidate;break;}
                        }
                    }
                }
                if(!component||!object->SetComponentIndex(component,index)) {reason="Runtime 拒绝恢复组件顺序。";return false;}
                if(data["ID"].isUInt64())_ObjectIDs.BindComponentFileID(component,data["ID"].asUInt64());
            }
        }
        std::vector<GameSproutRuntime::SceneComponentIdentity> componentIdentities;
        const auto collectComponentIdentities=[&](GameSproutRuntime::IGameObject& object) {
            for(std::size_t index=0;index<object.GetComponentCount();++index) {
                auto* component=object.GetComponentAt(index);
                const auto id=_ObjectIDs.FindComponentFileID(component);
                if(id)componentIdentities.push_back({component,id});
            }
        };
        collectComponentIdentities(*scene);
        for(const auto& node:document["Objects"])collectComponentIdentities(*objects.at(node["ID"].asUInt64()));
        if(!runtime->SetSceneComponentIdentities(scene,componentIdentities,reason))return false;
        // Surviving descendants have already moved. Destroy only obsolete roots;
        // Runtime owns recursive destruction and no node pointer escapes this call.
        std::vector<uint64_t> obsolete;
        for(const auto& pair:objects)if(!targetNodes.count(pair.first))obsolete.push_back(pair.second->GetInstanceID());
        for(const auto id:obsolete)if(auto* object=runtime->QueryGameObject(id)) {
            for(std::size_t i=0;i<object->GetComponentCount();++i)
                _ObjectIDs.ForgetComponentFileID(object->GetComponentAt(i));
            if(!scene->DestroyGameObject(object)) {reason="Runtime 拒绝删除文档中已移除的节点。";return false;}
        }
        std::map<uint64_t,std::size_t> siblingIndices;
        for(const auto& node:document["Objects"]) {
            const auto parent=node["ParentID"].asUInt64();
            if(!objects.at(node["ID"].asUInt64())->SetSiblingIndex(siblingIndices[parent]++)) {reason="Runtime 拒绝恢复节点顺序。";return false;}
        }
        if(!scene->SetLayer(static_cast<std::uint8_t>(document.get("RootLayer",0).asUInt()))||
           !scene->SetTagID(document.get("RootTagID",Json::UInt64(0)).asUInt64())) {
            reason="Runtime 拒绝场景根 Layer 或未定义的 TagID。";return false;
        }
        if(!scene->SetStaticFlags(static_cast<GameSproutRuntime::StaticFlags>(document.get("RootStaticFlags",0).asUInt()))) {
            reason="Runtime 拒绝场景根静态标志。";return false;
        }
        SceneAnimationDocument::Bind(document,*scene,objects);
        auto* camera=document.get("RootMainCamera",false).asBool()?scene->GetComponent<GameSproutRuntime::ICameraComponent>():
            document["MainCameraID"].asUInt64()?objects.at(document["MainCameraID"].asUInt64())->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr;
        if(!scene->SetMainCamera(camera)) {reason="Runtime 拒绝文档主相机。";return false;}
        const auto next=document["NextObjectID"].asUInt64();
        if(_ObjectIDs.Next&&(!next||next>_ObjectIDs.Next))_ObjectIDs.Next=next;
        Json::Value verified;
        return SceneDocument::Serialize(*scene,_ObjectIDs,verified,reason);
    };
    try {
        if(!runtime->BeginConstruction(error))return false;constructionOpen=true;
        if(!patch(after,error))throw std::runtime_error(error);
        constructionOpen=false;
        if(!runtime->EndConstruction(error))throw std::runtime_error(error.empty()?"Runtime 拒绝完成文档事务。":error);
        if(runtime->QueryGameObject(sceneID)!=scene)throw std::runtime_error("文档事务初始化期间场景已被删除。");
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Runtime 文档事务发生异常。";}
    try {
        std::string rollback;
        if(!constructionOpen)constructionOpen=runtime->BeginConstruction(rollback);
        if(!constructionOpen||runtime->QueryGameObject(sceneID)!=scene||!patch(before,rollback))error+=" 恢复原文档失败："+rollback;
        else _ObjectIDs.Next=previousNext;
    } catch(const std::exception& exception) {error+=" 恢复原文档失败："+std::string(exception.what());}
    catch(...) {error+=" 恢复原文档时发生异常。";}
    if(constructionOpen) {std::string cleanup;try {if(!runtime->EndConstruction(cleanup))error+=" 完成恢复事务失败："+cleanup;}catch(...) {error+=" 完成恢复事务时发生异常。";}}
    return false;
}

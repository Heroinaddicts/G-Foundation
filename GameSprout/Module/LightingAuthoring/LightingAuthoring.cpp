#include "LightingAuthoring.h"
#include "ISelectionManager.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "../Shared/AssetArtifactIO.h"
#include <QThread>
#include <limits>

namespace {
Json::Value Values(std::initializer_list<float> values) {
    Json::Value array(Json::arrayValue);for(float value:values)array.append(value);return array;
}
Json::Value LightDocument(const std::string& name) {
    Json::Value document(Json::objectValue),node(Json::objectValue),transform(Json::objectValue),light(Json::objectValue);
    document["Version"]=Json::UInt(GameSproutRuntime::SceneDocumentVersion);document["Name"]=name;document["NextObjectID"]=Json::UInt64(2);
    document["MainCameraID"]=Json::UInt64(0);document["Objects"]=Json::Value(Json::arrayValue);
    node["ID"]=Json::UInt64(1);node["ParentID"]=Json::UInt64(0);node["Name"]=name;node["Active"]=true;
    node["Components"]=Json::Value(Json::arrayValue);
    transform["Type"]="GameSproutRuntime.Transform";transform["Position"]=Values({0,0,0});
    transform["Rotation"]=Values({0,0,0,1});transform["Scale"]=Values({1,1,1});
    light["Type"]="GameSproutRuntime.Light";
    light["Settings"]["Type"]="Directional";light["Settings"]["Enabled"]=true;
    light["Settings"]["Color"]=Values({1,1,1,1});light["Settings"]["Intensity"]=1.0;
    light["Settings"]["CullingMask"]=Json::UInt(0xffffffffu);
    const GameSproutRuntime::LightShadowSettings shadows;
    auto& shadowData=light["Settings"]["Shadows"];
    shadowData["Enabled"]=shadows.Enabled;shadowData["Strength"]=shadows.Strength;shadowData["Distance"]=shadows.Distance;
    shadowData["Resolution"]=Json::UInt(shadows.Resolution);shadowData["CascadeCount"]=Json::UInt(shadows.CascadeCount);
    shadowData["DepthBias"]=shadows.DepthBias;shadowData["NormalBias"]=shadows.NormalBias;shadowData["Soft"]=shadows.Soft;
    shadowData["CacheStaticCasters"]=shadows.CacheStaticCasters;
    node["Components"].append(std::move(transform));node["Components"].append(std::move(light));
    document["Objects"].append(std::move(node));return document;
}
}
LightingAuthoring::~LightingAuthoring(){Release(nullptr);}
void LightingAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;if(!editor)return;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Objects=editor->GetGameSproutModule<IObjectEditing>("ObjectEditing");
    _Selection=editor->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");_Runtime=editor->_Runtime;
}
bool LightingAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();return _Scenes&&_Objects&&_Selection&&_Play&&_Host&&_Runtime;
}
GameSproutRuntime::IGameObject* LightingAuthoring::Parent(std::uint64_t id) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;auto* root=_Scenes?_Scenes->GetDocumentContentRoot():nullptr;
    auto* parent=(!id||(scene&&id==scene->GetInstanceID()))?root:(_Runtime?_Runtime->QueryGameObject(id):nullptr);
    if(!scene||!root||!parent||parent->GetScene()!=scene)return nullptr;
    for(auto* ancestor=parent;ancestor;ancestor=ancestor->Parent())if(ancestor==root)return parent;
    return nullptr;
}
bool LightingAuthoring::CanCreate(std::uint64_t sceneID,std::uint64_t parentID,std::string& error) const {
    error.clear();
    if(!_Runtime||!_Host||!_Scenes||!_Play||QThread::currentThread()!=_OwnerThread||_Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="方向光只能在所属主线程的 Runtime 空闲阶段创建。";return false;
    }
    if(_Play->GetState().State!=EditorPlayState::Edit||_Scenes->IsPlaySessionActive()){error="请先停止游戏，再创建方向光。";return false;}
    if(_Scenes->HasTransformEdit()||_Scenes->HasPropertyPreview()){error="请先结束当前连续编辑。";return false;}
    auto* scene=_Scenes->GetActiveScene();
    if(!scene||scene->GetInstanceID()!=sceneID||!Parent(parentID)){error="创建目标已失效或不属于当前文档。";return false;}
    return true;
}
bool LightingAuthoring::CreateDirectionalLight(std::uint64_t sceneID,std::uint64_t parentID,std::uint64_t& created,std::string& error) {
    created=0;if(!CanCreate(sceneID,parentID,error))return false;
    auto* parent=Parent(parentID);std::string name="Directional Light";
    for(std::uint64_t suffix=1;parent->FindChild(name);++suffix) {
        if(suffix==std::numeric_limits<std::uint64_t>::max()){error="方向光名称已耗尽。";return false;}
        name="Directional Light ("+std::to_string(suffix)+")";
    }
    SceneDocumentEditRequest request;request.ParentObjectID=parent->GetInstanceID();request.RemapDocumentIDs=true;
    request.Label="创建方向光";request.DocumentJson=AssetArtifactIO::JsonText(LightDocument(name));
    SceneSubtreeDocument result;if(!_Scenes->EditObjectDocument(request,result,error))return false;
    created=result.RootObjectID;_Selection->SetSceneObjectSelection({sceneID,created});return true;
}
GameSproutRuntime::ILightComponent* LightingAuthoring::Resolve(const ObjectEditTarget& target) const {
    if(!_Runtime||!_Scenes||QThread::currentThread()!=_OwnerThread)return nullptr;
    auto* scene=_Scenes->GetActiveScene();auto* object=_Runtime->QueryGameObject(target.ObjectID);
    if(!scene||target.SceneID!=scene->GetInstanceID()||!object||object->GetScene()!=scene)return nullptr;
    if(!target.ComponentID)return object->GetComponent<GameSproutRuntime::ILightComponent>();
    for(std::size_t index=0;index<object->GetComponentCount();++index) {
        auto* component=object->GetComponentAt(index);
        if(component&&component->GetComponentInstanceID()==target.ComponentID)return dynamic_cast<GameSproutRuntime::ILightComponent*>(component);
    }
    return nullptr;
}
bool LightingAuthoring::ReadLight(const ObjectEditTarget& target,GameSproutRuntime::LightSettings& value,std::string& error) const {
    error.clear();auto* light=Resolve(target);if(!light){error="方向光已失效或场景已切换。";return false;}
    value=light->GetSettings();return true;
}
bool LightingAuthoring::SetLight(const ObjectEditTarget& target,const GameSproutRuntime::LightSettings& value,std::string& error) {
    auto* light=Resolve(target);if(!light||!_Objects){error="方向光已失效或场景已切换。";return false;}
    const auto previous=light->GetSettings();
    const auto instanceID=light->GetComponentInstanceID();
    const auto fileID=_Scenes->GetComponentFileID(target.ObjectID,instanceID);
    const auto operation=[scenes=_Scenes,fileID,instanceID](GameSproutRuntime::LightSettings settings) {
        return [scenes,fileID,instanceID,settings](GameSproutRuntime::IGameObject& object,std::string& failure) {
            GameSproutRuntime::ILightComponent* current=nullptr;
            if(fileID)current=dynamic_cast<GameSproutRuntime::ILightComponent*>(scenes->QueryComponentByFileID(object.GetInstanceID(),fileID));
            else for(std::size_t index=0;index<object.GetComponentCount();++index) {
                auto* candidate=object.GetComponentAt(index);
                if(candidate&&candidate->GetComponentInstanceID()==instanceID){current=dynamic_cast<GameSproutRuntime::ILightComponent*>(candidate);break;}
            }
            if(current&&current->SetSettings(settings))return true;
            failure="方向光参数无效或组件已移除；请检查颜色、强度及阴影参数范围。";return false;
        };
    };
    return _Objects->EditObject(target,"修改方向光",operation(value),operation(previous),error);
}
void LightingAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    if(_RuntimeSubscribed&&_Runtime)_Runtime->UnregisterListener(this);
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _RuntimeSubscribed=false;_SceneSubscribed=false;_PlaySubscribed=false;
    _EnvironmentListeners.clear();_NextEnvironmentListener=0;_HaveEnvironmentSnapshot=false;_NotifyingEnvironment=false;
    _Scenes=nullptr;_Objects=nullptr;_Selection=nullptr;_Play=nullptr;_Host=nullptr;_Runtime=nullptr;_OwnerThread=nullptr;
}

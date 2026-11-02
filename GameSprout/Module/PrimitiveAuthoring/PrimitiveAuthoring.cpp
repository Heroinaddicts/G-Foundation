#include "PrimitiveAuthoring.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "../Shared/AssetArtifactIO.h"
#include <QThread>
#include <limits>

namespace {
Json::Value Values(std::initializer_list<float> values) {
    Json::Value array(Json::arrayValue);for(const auto value:values)array.append(value);return array;
}
Json::Value PrimitiveDocument(GameSproutRuntime::PrimitiveType type,const std::string& name) {
    Json::Value document(Json::objectValue),node(Json::objectValue),transform(Json::objectValue),mesh(Json::objectValue),renderer(Json::objectValue);
    document["Version"]=Json::UInt(1);document["Name"]=name;document["NextObjectID"]=Json::UInt64(2);
    document["MainCameraID"]=Json::UInt64(0);document["Objects"]=Json::Value(Json::arrayValue);
    node["ID"]=Json::UInt64(1);node["ParentID"]=Json::UInt64(0);node["Name"]=name;node["Active"]=true;
    node["Components"]=Json::Value(Json::arrayValue);
    transform["Type"]="GameSproutRuntime.Transform";transform["Position"]=Values({0,0,0});
    transform["Rotation"]=Values({0,0,0,1});transform["Scale"]=Values({1,1,1});
    const auto reference=GameSproutRuntime::GetBuiltinMeshReference(type);
    mesh["Type"]="GameSproutRuntime.Mesh";mesh["Mesh"]["Guid"]=reference.Guid;mesh["Mesh"]["LocalID"]=Json::UInt64(reference.LocalID);
    renderer["Type"]="GameSproutRuntime.MeshRenderer";renderer["Visible"]=true;renderer["Materials"]=Json::Value(Json::arrayValue);
    node["Components"].append(std::move(transform));node["Components"].append(std::move(mesh));node["Components"].append(std::move(renderer));
    const char* collider=type==GameSproutRuntime::PrimitiveType::Cube?"GameSproutRuntime.BoxCollider":
        type==GameSproutRuntime::PrimitiveType::Sphere?"GameSproutRuntime.SphereCollider":
        type==GameSproutRuntime::PrimitiveType::Capsule?"GameSproutRuntime.CapsuleCollider":nullptr;
    if(collider) {Json::Value component(Json::objectValue);component["Type"]=collider;node["Components"].append(std::move(component));}
    document["Objects"].append(std::move(node));return document;
}
}
PrimitiveAuthoring::~PrimitiveAuthoring(){Release(nullptr);}
void PrimitiveAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;if(!editor)return;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Selection=editor->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");_Runtime=editor->_Runtime;
}
bool PrimitiveAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();_Ready=_Scenes&&_Selection&&_Play&&_Host&&_Runtime;return _Ready;
}
GameSproutRuntime::IGameObject* PrimitiveAuthoring::Parent(std::uint64_t id) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;auto* root=_Scenes?_Scenes->GetDocumentContentRoot():nullptr;
    auto* parent=(!id||scene&&id==scene->GetInstanceID())?root:_Runtime->QueryGameObject(id);
    if(!scene||!root||!parent||parent->GetScene()!=scene)return nullptr;
    for(auto* ancestor=parent;ancestor;ancestor=ancestor->Parent())if(ancestor==root)return parent;
    return nullptr;
}
bool PrimitiveAuthoring::CanCreate(std::uint64_t sceneID,std::uint64_t parentID,std::string& error) const {
    error.clear();
    if(!_Ready||QThread::currentThread()!=_OwnerThread||_Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="基础物体只能在所属主线程的 Runtime 空闲阶段创建。";return false;
    }
    if(_Play->GetState().State!=EditorPlayState::Edit||_Scenes->IsPlaySessionActive()) {error="请先停止游戏，再创建基础物体。";return false;}
    if(_Scenes->HasTransformEdit()){error="请先完成或取消当前变换拖拽。";return false;}
    auto* scene=_Scenes->GetActiveScene();
    if(!scene||scene->GetInstanceID()!=sceneID||!Parent(parentID)){error="创建目标已失效或不属于当前文档。";return false;}
    return true;
}
bool PrimitiveAuthoring::CreatePrimitive(std::uint64_t sceneID,std::uint64_t parentID,GameSproutRuntime::PrimitiveType type,
    std::uint64_t& createdObjectID,std::string& error) {
    createdObjectID=0;if(!CanCreate(sceneID,parentID,error))return false;
    const char* base=GameSproutRuntime::PrimitiveName(type);if(!base){error="未知基础物体类型。";return false;}
    auto* parent=Parent(parentID);std::string name=base;
    for(std::uint64_t suffix=1;parent->FindChild(name);++suffix) {
        if(suffix==std::numeric_limits<std::uint64_t>::max()){error="基础物体名称已耗尽。";return false;}
        name=std::string(base)+" ("+std::to_string(suffix)+")";
    }
    SceneDocumentEditRequest request;request.ParentObjectID=parent->GetInstanceID();request.RemapDocumentIDs=true;
    request.Label="创建 "+std::string(base);request.DocumentJson=AssetArtifactIO::JsonText(PrimitiveDocument(type,name));
    SceneSubtreeDocument result;if(!_Scenes->EditObjectDocument(request,result,error))return false;
    createdObjectID=result.RootObjectID;
    _Selection->SetSceneObjectSelection({sceneID,createdObjectID});return true;
}
void PrimitiveAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Ready=false;_OwnerThread=nullptr;_Scenes=nullptr;_Selection=nullptr;_Play=nullptr;_Host=nullptr;_Runtime=nullptr;
}

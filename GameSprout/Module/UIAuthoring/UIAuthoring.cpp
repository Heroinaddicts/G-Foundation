#include "UIAuthoring.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "IObjectEditing.h"
#include "../Shared/AssetArtifactIO.h"
#include <QThread>
#include <limits>

namespace {
using GameSproutRuntime::ICanvasComponent;
using GameSproutRuntime::IGameObject;

const char* TemplateName(UIControlTemplate type) {
    switch(type) {
    case UIControlTemplate::Canvas: return "Canvas";
    case UIControlTemplate::Empty: return "UI Object";
    case UIControlTemplate::Panel: return "Panel";
    case UIControlTemplate::Image: return "Image";
    case UIControlTemplate::Text: return "Text";
    case UIControlTemplate::Button: return "Button";
    }
    return nullptr;
}

std::string UniqueName(IGameObject& parent,const char* base) {
    std::string name=base;
    for(std::uint64_t suffix=1;parent.FindChild(name);++suffix) {
        if(suffix==std::numeric_limits<std::uint64_t>::max())return {};
        name=std::string(base)+" ("+std::to_string(suffix)+")";
    }
    return name;
}

bool HasCanvas(IGameObject& node,IGameObject& documentRoot) {
    for(auto* ancestor=&node;ancestor;ancestor=ancestor->Parent()) {
        if(ancestor->GetComponent<ICanvasComponent>())return true;
        if(ancestor==&documentRoot)break;
    }
    return false;
}

IGameObject* FindScreenCanvas(IGameObject& root) {
    std::vector<IGameObject*> pending{&root};
    while(!pending.empty()) {
        auto* node=pending.back();
        pending.pop_back();
        if(!node->IsActiveInHierarchy())continue;
        if(auto* canvas=node->GetComponent<ICanvasComponent>();
            canvas&&canvas->GetRenderMode()==GameSproutRuntime::CanvasRenderMode::ScreenSpace)return node;
        for(std::size_t i=node->GetChildCount();i>0;--i)pending.push_back(node->GetChild(i-1));
    }
    return nullptr;
}

Json::Value Values(std::initializer_list<float> values) {
    Json::Value array(Json::arrayValue);
    for(float value:values)array.append(value);
    return array;
}

Json::Value Component(const char* type) {
    Json::Value component(Json::objectValue);
    component["Type"]=std::string("GameSproutRuntime.")+type;
    return component;
}

Json::Value Node(UIControlTemplate type,const std::string& name,std::uint64_t id,std::uint64_t parent,
    bool stretch=false,const char* label="Text") {
    Json::Value node(Json::objectValue);
    node["ID"]=Json::UInt64(id);
    node["ParentID"]=Json::UInt64(parent);
    node["Name"]=name;
    node["Active"]=true;
    node["Layer"]=Json::UInt(GameSproutRuntime::UILayer);
    auto& components=node["Components"];
    components=Json::Value(Json::arrayValue);
    components.append(Component("Transform"));

    auto rect=Component("RectTransform");
    const bool fill=stretch||type==UIControlTemplate::Panel||type==UIControlTemplate::Canvas;
    if(fill) {
        rect["AnchorMin"]=Values({0,0});
        rect["AnchorMax"]=Values({1,1});
        rect["SizeDelta"]=Values({0,0});
    } else if(type==UIControlTemplate::Button||type==UIControlTemplate::Text) {
        rect["SizeDelta"]=Values({220,64});
    }
    components.append(std::move(rect));

    switch(type) {
    case UIControlTemplate::Canvas:
        components.append(Component("Canvas"));
        break;
    case UIControlTemplate::Panel:
    case UIControlTemplate::Image:
    case UIControlTemplate::Button: {
        auto image=Component("Image");
        if(type==UIControlTemplate::Panel)image["Color"]=Values({0.05f,0.07f,0.10f,0.65f});
        if(type==UIControlTemplate::Button)image["Color"]=Values({0.06f,0.22f,0.46f,1});
        components.append(std::move(image));
        if(type==UIControlTemplate::Button)components.append(Component("Button"));
        break;
    }
    case UIControlTemplate::Text: {
        auto text=Component("Text");
        text["Text"]=label;
        text["FontSize"]=32;
        components.append(std::move(text));
        break;
    }
    case UIControlTemplate::Empty:
        break;
    }
    return node;
}

Json::Value Document(UIControlTemplate type,const std::string& name,const std::string& canvasName,
    std::uint64_t& controlID) {
    Json::Value document(Json::objectValue);
    document["Version"]=Json::UInt(GameSproutRuntime::SceneDocumentVersion);
    document["Name"]=name;
    document["MainCameraID"]=Json::UInt64(0);
    auto& nodes=document["Objects"];
    nodes=Json::Value(Json::arrayValue);
    std::uint64_t next=1;
    std::uint64_t parent=0;
    if(!canvasName.empty()) {
        parent=next++;
        nodes.append(Node(UIControlTemplate::Canvas,canvasName,parent,0));
    }
    controlID=next++;
    nodes.append(Node(type,name,controlID,parent));
    if(type==UIControlTemplate::Button)
        nodes.append(Node(UIControlTemplate::Text,"Text",next++,controlID,true,"Button"));
    document["NextObjectID"]=Json::UInt64(next);
    return document;
}
}

UIAuthoring::~UIAuthoring(){Release(nullptr);}

void UIAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    if(!editor)return;
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Selection=editor->GetGameSproutModule<ISelectionManager>("SelectionManager");
    _Play=editor->GetGameSproutModule<IPlayMode>("PlayMode");
    _Host=editor->GetGameSproutModule<IRuntimeHost>("RuntimeHost");
    _Editing=editor->GetGameSproutModule<IObjectEditing>("ObjectEditing");
    _Runtime=editor->_Runtime;
}

bool UIAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();
    return _Scenes&&_Selection&&_Play&&_Host&&_Editing&&_Runtime;
}

IGameObject* UIAuthoring::ResolveParent(std::uint64_t id) const {
    auto* scene=_Scenes->GetActiveScene();
    auto* root=_Scenes->GetDocumentContentRoot();
    if(!scene||!root)return nullptr;
    if(!id||id==scene->GetInstanceID())return root;
    auto* node=_Runtime->QueryGameObject(id);
    if(!node||node->GetScene()!=scene)return nullptr;
    for(auto* ancestor=node;ancestor;ancestor=ancestor->Parent())if(ancestor==root)return node;
    return nullptr;
}

bool UIAuthoring::CanCreate(std::uint64_t sceneID,std::uint64_t parentID,std::string& error) const {
    error.clear();
    if(!_OwnerThread||QThread::currentThread()!=_OwnerThread||!_Runtime||!_Host||!_Scenes||!_Play||_Runtime->IsExecuting()||_Host->IsFrameActive()) {
        error="只能在编辑器主线程的 Runtime 空闲阶段创建游戏 UI。";
        return false;
    }
    if(_Play->GetState().State!=EditorPlayState::Edit||_Scenes->IsPlaySessionActive()) {
        error="请先停止游戏，再创建 UI 控件。";
        return false;
    }
    if(_Scenes->HasTransformEdit()||_Scenes->HasPropertyPreview()) {
        error="请先结束当前连续编辑。";
        return false;
    }
    auto* scene=_Scenes->GetActiveScene();
    if(!scene||sceneID!=scene->GetInstanceID()||!ResolveParent(parentID)) {
        error="创建目标已失效或不属于当前文档。";
        return false;
    }
    return true;
}

bool UIAuthoring::CreateControl(std::uint64_t sceneID,std::uint64_t parentID,UIControlTemplate type,
    std::uint64_t& created,std::string& error) {
    created=0;
    if(!CanCreate(sceneID,parentID,error))return false;
    const char* base=TemplateName(type);
    if(!base){error="未知的 UI 控件模板。";return false;}
    auto* root=_Scenes->GetDocumentContentRoot();
    auto* parent=ResolveParent(parentID);
    std::string wrapper;
    if(type!=UIControlTemplate::Canvas&&!HasCanvas(*parent,*root)) {
        // A context click on a concrete node keeps the new subtree there.
        // At the document root, reuse an active screen-space Canvas when available.
        auto* canvas=parent==root?FindScreenCanvas(*root):nullptr;
        if(canvas)parent=canvas;
        else {
            wrapper=UniqueName(*parent,"Canvas");
            if(wrapper.empty()){error="无法为 Canvas 分配同级唯一名称。";return false;}
        }
    }
    const auto name=wrapper.empty()?UniqueName(*parent,base):std::string(base);
    if(name.empty()){error="无法为控件分配同级唯一名称。";return false;}
    std::uint64_t localID=0;
    SceneDocumentEditRequest request;
    request.ParentObjectID=parent->GetInstanceID();
    request.RemapDocumentIDs=true;
    request.Label="创建 UI "+std::string(base);
    request.DocumentJson=AssetArtifactIO::JsonText(Document(type,name,wrapper,localID));
    SceneSubtreeDocument result;
    if(!_Scenes->EditObjectDocument(request,result,error))return false;
    for(const auto& identity:result.ObjectIDs) {
        if(!identity.Component&&identity.DocumentFileID==localID) {
            created=identity.RuntimeObjectID;
            break;
        }
    }
    if(created)_Selection->SetSceneObjectSelection({sceneID,created});
    return true;
}

void UIAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    std::string ignored;
    CancelRectEdit(ignored);
    _OwnerThread=nullptr;
    _Scenes=nullptr;
    _Selection=nullptr;
    _Play=nullptr;
    _Host=nullptr;
    _Editing=nullptr;
    _Runtime=nullptr;
}

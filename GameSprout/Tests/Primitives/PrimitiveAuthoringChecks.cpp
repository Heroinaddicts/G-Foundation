#include "PrimitiveAuthoring.h"
#include "PrimitiveUI.h"
#include "SceneManager.h"
#include "IHierarchyUI.h"
#include "ISelectionManager.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "IProject.h"
#include "ResourceField.h"
#include "GameSproutRuntimeExport.h"
#include "PrefabDocument.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QAction>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace GameSproutEditor {class GameSproutEditorFinal {public:
    static bool Start(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);return module.Initialize(&api)&&module.Launch(&api);}
    static bool Start(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);return view.Initialize(&api)&&view.Launch(&api);}
    static void Stop(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
    static void Stop(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
};}
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class T>class Module:public T{
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Project final:public Module<IProject>{public:std::string Root;const std::string& GetProjectRoot()const override{return Root;}};
class Mode final:public Module<IPlayMode>{public:
    PlayModeState State;PlayModeState GetState()const override{return State;}
    bool Play(std::string&)override{return false;}bool Pause(std::string&)override{return false;}bool Resume(std::string&)override{return false;}bool Step(std::string&)override{return false;}bool Stop(std::string&)override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener*)override{return true;}void UnregisterPlayModeListener(IPlayModeListener*)override{}
};
class Host final:public Module<IRuntimeHost>{public:
    bool Frame=false;bool IsFrameActive()const override{return Frame;}
    void SetSimulationPlaying(bool)override{}bool IsSimulationPlaying()const override{return false;}void ResetClock()override{}
    bool RegisterFrameListener(IRuntimeFrameListener*)override{return true;}void UnregisterFrameListener(IRuntimeFrameListener*)override{}
};
class Selection final:public Module<ISelectionManager>{public:
    SceneObjectSelection Target;std::vector<ProjectAssetSelection> GetAssetSelection()const override{return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>&)override{}
    SceneObjectSelection GetSceneObjectSelection()const override{return Target;}
    void SetSceneObjectSelection(const SceneObjectSelection& target)override{Target=target;}
    bool RegisterSelectionListener(ISelectionListener*)override{return true;}void UnregisterSelectionListener(ISelectionListener*)override{}
};
class Hierarchy final:public IHierarchyUI{public:
    struct Action{const void* Owner;std::vector<std::string> Path;fContextActionCallback Callback;fContextActionEnabled Enabled;};
    std::vector<Action> Actions;HierarchyActionContext Context;
    std::uint64_t GetSelectedObjectID()const override{return Context.ObjectID;}
    bool RegisterContextAction(const void* owner,const std::vector<std::string>& path,fContextActionCallback callback,const fContextActionEnabled& enabled)override{Actions.push_back({owner,path,callback,enabled});return true;}
    HierarchyActionContext GetActionContext()const override{return Context;}
    void UnregisterExtensions(const void* owner)override{std::erase_if(Actions,[owner](const Action& action){return action.Owner==owner;});}
    void Show()override{}void Hide()override{}
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutViewApi*)override{return true;}void Release(GameSproutEditor::GameSproutViewApi*)override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi{public:
    Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;Hierarchy* Tree=nullptr;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{const auto entry=Modules.find(name);return entry==Modules.end()?nullptr:entry->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name)const override{return name=="HierarchyUI"?Tree:nullptr;}
};
int main(int argc,char** argv){
    QApplication application(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try{
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime unavailable");std::string error;
        QTemporaryDir temporary;Check(temporary.isValid()&&QDir(temporary.path()).mkpath("Assets"),"No isolated project");
        Project project;project.Root=temporary.path().toStdString();Host host;Mode mode;Selection selection;Hierarchy hierarchy;Editor editor(runtime);editor.Tree=&hierarchy;
        SceneManager scenes;PrimitiveAuthoring authoring;PrimitiveUI view;
        editor.Modules={{"Project",&project},{"RuntimeHost",&host},{"PlayMode",&mode},{"SelectionManager",&selection},{"SceneManager",&scenes},{"PrimitiveAuthoring",&authoring}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(scenes,api),"No scene module");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(authoring,api),"No primitive module");
        hierarchy.Context={scenes.GetActiveScene()->GetInstanceID(),0};
        Check(GameSproutEditor::GameSproutEditorFinal::Start(view,editor)&&hierarchy.Actions.size()==6,"Six .ui creation actions did not register");
        for(const auto& action:hierarchy.Actions)Check(action.Path.size()==2&&action.Path[0]=="3D Object"&&action.Enabled(hierarchy.Context),"Creation menu disabled or path incorrect");
        hierarchy.Actions.front().Callback();auto* cube=scenes.GetActiveScene()->FindChild("Cube");Check(cube,"Menu did not create cube");
        Check(cube->GetComponent<GameSproutRuntime::IBoxColliderComponent>()&&cube->GetComponent<GameSproutRuntime::IMeshRendererComponent>(),"Cube default components missing");
        const auto cubeFile=scenes.GetObjectFileID(cube->GetInstanceID());Check(selection.Target.ObjectID==cube->GetInstanceID()&&cubeFile,"New primitive was not selected or has no stable ID");
        std::uint64_t created=0;
        Check(authoring.CreatePrimitive(hierarchy.Context.SceneID,cube->GetInstanceID(),GameSproutRuntime::PrimitiveType::Sphere,created,error),error);
        Check(cube->FindChild("Sphere")&&cube->FindChild("Sphere")->GetComponent<GameSproutRuntime::ISphereColliderComponent>(),"Child primitive shape or parent incorrect");
        Check(scenes.Undo(error)&&!cube->FindChild("Sphere"),error);Check(scenes.Redo(error)&&cube->FindChild("Sphere"),error);
        for(unsigned type=1;type<6;++type)Check(authoring.CreatePrimitive(hierarchy.Context.SceneID,0,static_cast<GameSproutRuntime::PrimitiveType>(type),created,error),error);
        Check(authoring.CreatePrimitive(hierarchy.Context.SceneID,0,GameSproutRuntime::PrimitiveType::Cube,created,error)&&scenes.GetActiveScene()->FindChild("Cube (1)"),"Sibling primitive name collision");
        Check(!authoring.CreatePrimitive(hierarchy.Context.SceneID+123,0,GameSproutRuntime::PrimitiveType::Cube,created,error)&&created==0,"Stale scene command accepted");
        host.Frame=true;Check(!authoring.CanCreate(hierarchy.Context.SceneID,0,error),"Creation accepted within Host frame");host.Frame=false;
        mode.State.State=EditorPlayState::Playing;Check(!hierarchy.Actions.front().Enabled(hierarchy.Context),"Creation enabled during Play");mode.State.State=EditorPlayState::Edit;
        bool rejected=false;std::thread worker([&]{std::string reason;rejected=!authoring.CanCreate(hierarchy.Context.SceneID,0,reason);});worker.join();Check(rejected,"Creation accepted on foreign thread");
        ResourceField field;field.Bind(nullptr,nullptr,&selection,eAssetType::Mesh);field.SetValue(GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Cube));
        Check(field.findChild<QLineEdit*>("resourceNameEdit")->text()=="Cube"&&!field.findChild<QAction*>("locateResourceAction")->isEnabled(),"Built-in resource displays as Missing or permits invalid source navigation");
        Check(scenes.SaveSceneAs("Primitives.scene",error),error);Check(scenes.OpenScene("Primitives.scene",error),error);
        cube=scenes.QueryObjectByFileID(cubeFile);Check(cube&&cube->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Cube),"Scene round trip lost stable built-in mesh");
        SceneSubtreeDocument captured;Check(scenes.CaptureObjectDocument(cube->GetInstanceID(),captured,error),error);
        Json::Value data;Json::Reader reader;Check(reader.parse(captured.DocumentJson,data),"Cannot read captured subtree");
        GameSproutRuntime::PrefabData prepared;Check(PrefabDocument::Encode(data,prepared.Bytes,error),error);
        auto* destination=runtime->CreateScene("Packaged runtime");std::vector<GameSproutRuntime::PrefabObjectMapping> mappings;
        auto* instance=runtime->InstantiatePrefabData(destination,prepared,nullptr,mappings,error);Check(instance,error);
        Check(instance->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Cube)&&instance->FindChild("Sphere"),"Prefab instantiation lost built-in reference or child");
        Check(runtime->DestroyScene(destination),"Cannot release isolated packaged scene");
        GameSproutEditor::GameSproutEditorFinal::Stop(view,editor);Check(hierarchy.Actions.empty(),"Released primitive View kept menu callbacks");
        GameSproutEditor::GameSproutEditorFinal::Stop(authoring,api);GameSproutEditor::GameSproutEditorFinal::Stop(scenes,api);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS primitive .ui menu routing, transactions, parent/selection, Undo/Redo, guardrails, built-in fields, scene and Prefab round trips\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';delete runtime;return 1;}
}

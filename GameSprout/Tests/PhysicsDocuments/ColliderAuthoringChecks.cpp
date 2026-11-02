#include "PhysicsAuthoring.h"
#include "ObjectEditing.h"
#include "SceneManager.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeBuiltinResources.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <map>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace GameSproutEditor {
class GameSproutEditorFinal {public:
    static bool Start(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);return module.Initialize(&api)&&module.Launch(&api);}
    static void Stop(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class T>class Service:public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Project final:public Service<IProject> {public:std::string Root;const std::string& GetProjectRoot()const override{return Root;}};
class Mode final:public Service<IPlayMode> {public:
    PlayModeState State;std::vector<IPlayModeListener*> Listeners;
    PlayModeState GetState()const override{return State;}
    bool Play(std::string&)override{return false;}bool Pause(std::string&)override{return false;}bool Resume(std::string&)override{return false;}bool Step(std::string&)override{return false;}bool Stop(std::string&)override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterPlayModeListener(IPlayModeListener* listener)override{std::erase(Listeners,listener);}
    void Change(EditorPlayState value){State.State=value;for(auto* listener:Listeners)listener->OnPlayModeStateChanged(State);}
};
class Host final:public Service<IRuntimeHost> {public:
    bool Frame=false;bool IsFrameActive()const override{return Frame;}
    void SetSimulationPlaying(bool)override{}bool IsSimulationPlaying()const override{return false;}void ResetClock()override{}
    bool RegisterFrameListener(IRuntimeFrameListener*)override{return true;}void UnregisterFrameListener(IRuntimeFrameListener*)override{}
};
class Selection final:public Service<ISelectionManager> {public:
    SceneObjectSelection Current;std::vector<ISelectionListener*> Listeners;
    std::vector<ProjectAssetSelection> GetAssetSelection()const override{return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>& value)override{for(auto* listener:Listeners)listener->OnAssetSelectionChanged(value);}
    SceneObjectSelection GetSceneObjectSelection()const override{return Current;}
    void SetSceneObjectSelection(const SceneObjectSelection& value)override{Current=value;for(auto* listener:Listeners)listener->OnSceneObjectSelectionChanged(value);}
    bool RegisterSelectionListener(ISelectionListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterSelectionListener(ISelectionListener* listener)override{std::erase(Listeners,listener);}
};
class Assets final:public Service<IAssetsManager> {public:
    IAssetMeta* QueryAsset(const Guid&)const override{return nullptr;}bool ReimportAsset(const Guid&)override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&)const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&)override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas()const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&)const override{return {};}
    bool RegisterAssetListener(IAssetListener*)override{return true;}void UnregisterAssetListener(IAssetListener*)override{}
};
class Library final:public Service<ILibraryManager> {public:
    std::string Root;ILDBSession* GetLDBSession()const override{return nullptr;}const std::string& GetLibraryRoot()const override{return Root;}
    std::vector<std::string> GetManifestGuids()const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&)const override{return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&)override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&)override{return false;}
    std::vector<std::string> FindDependents(const std::string&)const override{return {};}
    IAsset* QueryAsset(const Guid&,uint64_t)override{return nullptr;}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&)const override{return nullptr;}
};
QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read fixture");return file.readAll();}
class Reentrant final:public IObjectEditingListener {public:
    ObjectEditing* Objects=nullptr;ObjectEditTarget Target;unsigned Calls=0;bool Accepted=false;
    void OnObjectPropertiesChanged(const ObjectEditEvent&)override {
        ++Calls;std::string error;
        Accepted=Accepted||Objects->EditObject(Target,"must reject reentry",[](auto&,auto&){return true;},[](auto&,auto&){return true;},error);
    }
};
}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass current Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime load failed");
        QTemporaryDir temporary;Check(temporary.isValid()&&QDir(temporary.path()).mkpath("Assets"),"Cannot create isolated project");
        Project project;project.Root=temporary.path().toStdString();Mode mode;Host host;Assets assets;Library library;Selection selection;
        SceneManager scenes;ObjectEditing objects;PhysicsAuthoring physics;Editor editor(runtime);
        editor.Modules={{"Project",&project},{"PlayMode",&mode},{"RuntimeHost",&host},{"AssetsManager",&assets},{"LibraryManager",&library},{"SceneManager",&scenes},{"ObjectEditing",&objects},{"PhysicsAuthoring",&physics},{"SelectionManager",&selection}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(scenes,api),"SceneManager startup");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(objects,api),"ObjectEditing startup");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(physics,api),"PhysicsAuthoring startup");
        auto* object=scenes.GetActiveScene()->CreateGameObject("Collider target");
        auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();mesh->SetMesh(GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Cube));
        auto* first=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        auto* box=object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
        Check(first&&box&&first->SetShape({{3,3,3}})&&box->SetShape({{7,7,7}}),"Create repeated collider fixture");
        auto* empty=scenes.GetActiveScene()->CreateGameObject("No mesh");auto* sphere=empty->AddComponent<GameSproutRuntime::ISphereColliderComponent>();sphere->SetShape({3});
        std::string error;Check(scenes.SaveSceneAs("Collider.scene",error),error);
        Check(!physics.FitCollider({scenes.GetActiveScene()->GetInstanceID(),empty->GetInstanceID(),sphere->GetComponentInstanceID()},error)&&sphere->GetShape().Radius==3,
            "Missing fitting geometry overwrote manual parameters");
        const auto objectFile=scenes.GetObjectFileID(object->GetInstanceID());
        const auto componentFile=scenes.GetComponentFileID(object->GetInstanceID(),box->GetComponentInstanceID());
        ColliderEditTarget target{scenes.GetActiveScene()->GetInstanceID(),object->GetInstanceID(),box->GetComponentInstanceID()};
        selection.SetSceneObjectSelection({target.SceneID,target.ObjectID});
        Check(physics.FitCollider(target,error)&&box->GetShape().Size.X==1&&first->GetShape().Size.X==3,"Fit selected the wrong repeated Collider: "+error);
        Check(scenes.Undo(error)&&box->GetShape().Size.X==7,"Fit Undo failed: "+error);
        Check(scenes.Redo(error)&&box->GetShape().Size.X==1,"Fit Redo failed: "+error);
        Check(scenes.SaveScene(error),error);
        const auto disk=Read(QDir(temporary.path()).filePath("Assets/Collider.scene"));
        ColliderEditState before;Check(physics.ReadCollider(target,before,error),error);
        host.Frame=true;
        ColliderEditState read;Check(physics.ReadCollider(target,read,error)&&!physics.CanEditCollider(target,error),"Read must allow Host frame; mutation must reject it");host.Frame=false;
        Reentrant reentrant;reentrant.Objects=&objects;reentrant.Target={target.SceneID,target.ObjectID,target.ComponentID};objects.RegisterEditingListener(&reentrant);
        Check(physics.BeginColliderEdit(target,error),error);auto next=before;next.Box.Size={2,3,4};next.Settings.Center={.2f,.3f,.4f};
        Check(physics.PreviewColliderEdit(next,error)&&box->GetShape().Size.Y==3,error);
        next.Box.Size={3,4,5};Check(physics.PreviewColliderEdit(next,error),error);
        Check(scenes.HasPropertyPreview()&&!scenes.SaveScene(error)&&!scenes.Undo(error),"Active preview permitted save or Undo");
        SceneSubtreeDocument blocked;
        Check(!scenes.CaptureObjectDocument(target.ObjectID,blocked,error),"Prefab/source capture accepted uncommitted preview");
        Check(physics.CommitColliderEdit(error)&&!scenes.HasPropertyPreview()&&box->GetShape().Size.Z==5,error);
        Check(scenes.Undo(error)&&box->GetShape().Size.X==1&&box->GetColliderSettings().Center.X==0,"Whole drag must be one Undo");
        Check(scenes.CanRedo(),"Undo did not create Redo");
        Check(physics.BeginColliderEdit(target,error)&&physics.PreviewColliderEdit(next,error),error);
        Check(physics.CancelColliderEdit(error)&&scenes.CanRedo()&&box->GetShape().Size.X==1,"Cancel truncated Redo or kept preview");
        Check(physics.BeginColliderEdit(target,error)&&physics.PreviewColliderEdit(next,error),error);
        host.Frame=true;Check(!physics.CancelColliderEdit(error)&&physics.HasColliderEdit()&&scenes.HasPropertyPreview(),"Busy cancel discarded original/lock");
        host.Frame=false;QCoreApplication::processEvents();
        Check(!physics.HasColliderEdit()&&!scenes.HasPropertyPreview()&&box->GetShape().Size.X==1,"Queued safe-point cancel did not restore original");
        Check(physics.BeginColliderEdit(target,error)&&physics.PreviewColliderEdit(next,error),error);
        selection.SetSceneObjectSelection({target.SceneID,empty->GetInstanceID()});
        Check(!physics.HasColliderEdit()&&box->GetShape().Size.X==1&&!scenes.HasPropertyPreview(),"Selection change did not restore active preview");
        selection.SetSceneObjectSelection({target.SceneID,target.ObjectID});
        Check(physics.BeginColliderEdit(target,error)&&physics.PreviewColliderEdit(next,error),error);
        mode.Change(EditorPlayState::EnteringPlay);
        Check(!physics.HasColliderEdit()&&box->GetShape().Size.X==1&&!scenes.HasPropertyPreview(),"Play transition did not restore active preview");
        mode.Change(EditorPlayState::Edit);
        Check(reentrant.Calls>0&&!reentrant.Accepted,"Editing notification allowed reentry");objects.UnregisterEditingListener(&reentrant);
        Check(Read(QDir(temporary.path()).filePath("Assets/Collider.scene"))==disk,"Preview wrote source file");
        Check(scenes.BeginPlaySession(error),error);mode.Change(EditorPlayState::Playing);
        object=scenes.QueryObjectByFileID(objectFile);box=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(scenes.QueryComponentByFileID(object->GetInstanceID(),componentFile));
        Check(box,"Play did not restore collider identity");target={scenes.GetActiveScene()->GetInstanceID(),object->GetInstanceID(),box->GetComponentInstanceID()};
        Check(physics.ReadCollider(target,before,error),error);next=before;next.Box.Size={2,3,4};
        Check(physics.BeginColliderEdit(target,error)&&physics.PreviewColliderEdit(next,error)&&physics.CommitColliderEdit(error),error);
        Check(!scenes.HasUnsavedChanges()&&Read(QDir(temporary.path()).filePath("Assets/Collider.scene"))==disk,"Play edit dirtied author/source");
        mode.Change(EditorPlayState::ExitingPlay);Check(scenes.EndPlaySession(error),error);mode.Change(EditorPlayState::Edit);
        object=scenes.QueryObjectByFileID(objectFile);box=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(scenes.QueryComponentByFileID(object->GetInstanceID(),componentFile));
        Check(box&&box->GetShape().Size.X==1,"Play state leaked into author collider");
        target={scenes.GetActiveScene()->GetInstanceID(),object->GetInstanceID(),box->GetComponentInstanceID()};
        Check(physics.BeginColliderEdit(target,error),error);
        Check(scenes.GetActiveScene()->DestroyGameObject(object),"Delete active collider target");
        Check(physics.CancelColliderEdit(error)&&!scenes.HasPropertyPreview(),"Deleted target left dangling preview reservation");
        Check(scenes.OpenScene("Collider.scene",error),error);
        GameSproutEditor::GameSproutEditorFinal::Stop(physics,api);GameSproutEditor::GameSproutEditorFinal::Stop(objects,api);GameSproutEditor::GameSproutEditorFinal::Stop(scenes,api);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS Collider fit, repeated identity, continuous preview lock, one Undo, cancel/Redo, busy safe-point rollback, no reentry and transient Play\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';delete runtime;return 1;}
}

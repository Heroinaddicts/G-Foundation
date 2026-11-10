#include "LightingAuthoring.h"
#include "LightingUI.h"
#include "LightComponentEditor.h"
#include "IHierarchyUI.h"
#include "IInspectorUI.h"
#include "IIDEWindow.h"
#include "ISelectionManager.h"
#include <QApplication>
#include <QLineEdit>
#include <QCheckBox>
#include <QGroupBox>
#include <QComboBox>
#include <QTest>
#include "ObjectEditing.h"
#include "SceneManager.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeBuiltinResources.h"
#include "SceneDocument.h"
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
    static bool Start(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);return view.Initialize(&api)&&view.Launch(&api);}
    static void Stop(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
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
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name)const override {const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;}
};
QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read fixture");return file.readAll();}

template<class T>class ViewService:public T {
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*)override{}
};
class Hierarchy final:public ViewService<IHierarchyUI> {public:
    HierarchyActionContext Context;bool Unregistered=false;
    std::map<std::vector<std::string>,fContextActionCallback> Actions;
    std::uint64_t GetSelectedObjectID()const override{return Context.ObjectID;}
    bool RegisterContextAction(const void*,const std::vector<std::string>& path,fContextActionCallback callback,const fContextActionEnabled&)override{return Actions.emplace(path,callback).second;}
    HierarchyActionContext GetActionContext()const override{return Context;}
    void UnregisterExtensions(const void*)override{Unregistered=true;Actions.clear();}
};
class Inspector final:public ViewService<IInspectorUI> {public:
    IInspectorExtension* Extension=nullptr;bool Unregistered=false;
    bool HasPendingChanges()const override{return false;}bool ApplyChanges()override{return false;}void RevertChanges()override{}
    bool RegisterExtension(IInspectorExtension* value)override{Extension=value;return true;}
    void UnregisterExtension(IInspectorExtension* value)override{if(Extension==value)Extension=nullptr;Unregistered=true;}
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*)override{return nullptr;}
};
class Menu final:public IIDEMenu {public:
    IIDEMenu* CreateChildMenu(const QString&)override{return this;}
    void SetClickCallback(fMenuClickCallback value)override{Callback=value;}
    fMenuClickCallback Callback=nullptr;
};
class MenuBar final:public IIDEMenuBar {public:
    Menu Item;IIDEMenu* CreateChildMenu(const QString&)override{return &Item;}
};
class IDE final:public ViewService<IIDEWindow> {public:
    mutable MenuBar Bar;IIDEMenuBar* GetMenuBar()const override{return &Bar;}
    IIDEDocker* CreateDocker(const QString&)override{return nullptr;}IIDEDocker* QueryDocker(const QString&)override{return nullptr;}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation)override{return false;}
    void Loading(const std::string&,const std::string&)override{}void Unloading()override{}void SetLoadingInfo(const std::string&,const std::string&)override{}
};
class EnvironmentRecorder final:public ILightingEnvironmentListener {public:
    std::vector<EnvironmentLightingSnapshot> Events;
    void OnEnvironmentLightingChanged(const EnvironmentLightingSnapshot& value)override{Events.push_back(value);}
};
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Pass current Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime load failed");
        QTemporaryDir temporary;Check(temporary.isValid()&&QDir(temporary.path()).mkpath("Assets"),"Cannot create isolated project");
        Project project;project.Root=temporary.path().toStdString();Mode mode;Host host;Assets assets;Library library;Selection selection;
        SceneManager scenes;ObjectEditing objects;LightingAuthoring lighting;Hierarchy hierarchy;Inspector inspector;IDE ide;LightingUI view;Editor editor(runtime);
        editor.Modules={{"Project",&project},{"PlayMode",&mode},{"RuntimeHost",&host},{"AssetsManager",&assets},{"LibraryManager",&library},{"SceneManager",&scenes},{"ObjectEditing",&objects},{"LightingAuthoring",&lighting},{"SelectionManager",&selection}};
        editor.Views={{"HierarchyUI",&hierarchy},{"InspectorUI",&inspector},{"IDEWindow",&ide}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(scenes,api),"SceneManager startup");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(objects,api),"ObjectEditing startup");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(lighting,api),"LightingAuthoring startup");
        EnvironmentRecorder environmentEvents;EnvironmentLightingSnapshot environment;std::string environmentError;
        Check(lighting.RegisterEnvironmentListener(&environmentEvents)&&!lighting.RegisterEnvironmentListener(&environmentEvents),"Environment listener identity validation failed");
        Check(lighting.ReadEnvironment(environment,environmentError)&&environment.Available&&environment.Editable&&
            environment.Settings.Mode==GameSproutRuntime::AmbientLightMode::None,"Initial environment snapshot invalid");
        auto environmentValue=environment.Settings;environmentValue.Mode=GameSproutRuntime::AmbientLightMode::Gradient;
        environmentValue.SkyColor={2,.4f,.2f,1};environmentValue.Intensity=2;
        Check(lighting.SetEnvironment(environment.SceneID,environmentValue,environmentError),environmentError);
        Check(!environmentEvents.Events.empty()&&environmentEvents.Events.back().Settings.Intensity==2,"Environment mutation did not push values");
        Check(scenes.Undo(environmentError)&&scenes.GetActiveScene()->GetEnvironmentLighting().Mode==GameSproutRuntime::AmbientLightMode::None,"Environment Undo failed: "+environmentError);
        Check(scenes.Redo(environmentError)&&scenes.GetActiveScene()->GetEnvironmentLighting().SkyColor.R==2,"Environment Redo failed: "+environmentError);
        environmentValue.Atmosphere.Enabled=true;environmentValue.Atmosphere.FogStart=120;
        const auto beforeAtmosphere=environmentEvents.Events.size();
        Check(lighting.SetEnvironment(environment.SceneID,environmentValue,environmentError),environmentError);
        Check(environmentEvents.Events.size()==beforeAtmosphere+1&&environmentEvents.Events.back().Settings.Atmosphere.FogStart==120,
            "Atmosphere-only transaction did not publish its new value");
        Check(scenes.Undo(environmentError)&&!scenes.GetActiveScene()->GetEnvironmentLighting().Atmosphere.Enabled,"Atmosphere Undo failed: "+environmentError);
        Check(scenes.Redo(environmentError)&&scenes.GetActiveScene()->GetEnvironmentLighting().Atmosphere.Enabled,"Atmosphere Redo failed: "+environmentError);
        auto invalidEnvironment=environmentValue;invalidEnvironment.Intensity=-1;
        Check(!lighting.SetEnvironment(environment.SceneID,invalidEnvironment,environmentError)&&scenes.GetActiveScene()->GetEnvironmentLighting().Intensity==2,"Invalid environment changed source");
        host.Frame=true;Check(!lighting.SetEnvironment(environment.SceneID,environmentValue,environmentError),"Host-frame environment mutation accepted");host.Frame=false;
        Check(!lighting.SetEnvironment(environment.SceneID+100,environmentValue,environmentError),"Stale environment scene identity accepted");
        Check(GameSproutEditor::GameSproutEditorFinal::Start(view,editor),"LightingUI startup");
        Check(hierarchy.Actions.contains({"Light","Directional Light"})&&inspector.Extension,"Directional Light menu/Inspector not registered");
        hierarchy.Context={scenes.GetActiveScene()->GetInstanceID(),0};hierarchy.Actions.at({"Light","Directional Light"})();
        auto selected=selection.Current;auto* object=runtime->QueryGameObject(selected.ObjectID);
        auto* light=object?object->GetComponent<GameSproutRuntime::ILightComponent>():nullptr;
        Check(light&&object->GetName()=="Directional Light","Designer menu did not create a Light transaction");
        const auto firstID=object->GetInstanceID();std::string error;
        Check(scenes.Undo(error)&&!runtime->QueryGameObject(firstID),"Creation Undo failed: "+error);
        Check(scenes.Redo(error),"Creation Redo failed: "+error);object=scenes.GetActiveScene()->FindChild("Directional Light");
        Check(object,"Redo did not restore Light");light=object->GetComponent<GameSproutRuntime::ILightComponent>();
        Check(scenes.SaveSceneAs("Light.scene",error),error);
        auto target=ObjectEditTarget{scenes.GetActiveScene()->GetInstanceID(),object->GetInstanceID(),light->GetComponentInstanceID()};
        auto value=light->GetSettings();value.Intensity=3;value.Color={.2f,.4f,.8f,1};value.CullingMask=4;
        Check(lighting.SetLight(target,value,error)&&light->GetSettings().Intensity==3,error);
        Check(scenes.Undo(error)&&light->GetSettings().Intensity==1,"Light edit Undo failed: "+error);
        Check(scenes.Redo(error)&&light->GetSettings().Intensity==3,"Light edit Redo failed: "+error);
        const auto before=light->GetSettings();value.Intensity=-1;
        Check(!lighting.SetLight(target,value,error)&&light->GetSettings().Intensity==before.Intensity,"Invalid Light changed source");
        host.Frame=true;Check(!lighting.SetLight(target,before,error),"Host-frame Light mutation accepted");host.Frame=false;
        auto* panel=view.CreateComponentEditor(typeid(GameSproutRuntime::ILightComponent),nullptr);Check(panel,"Light component editor missing");
        ComponentEditContext context;context.Runtime=runtime;context.Scenes=&scenes;context.Editing=&objects;
        context.SceneID=target.SceneID;context.ObjectID=target.ObjectID;context.ComponentID=target.ComponentID;
        context.Report=[&](const std::string& e){error=e;};panel->Bind(context);panel->Load(*light);panel->show();
        for(int width:{280,300}) {
            panel->resize(width,280);QCoreApplication::processEvents();
            Check(panel->width()==width&&panel->minimumSizeHint().width()<=width,"Light Inspector forces horizontal growth at "+std::to_string(width)+" px");
            for(QWidget* field:panel->findChildren<QWidget*>())if(field->isVisibleTo(panel)) {
                const QRect bounds(field->mapTo(panel,QPoint(0,0)),field->size());
                Check(bounds.left()>=0&&bounds.right()<panel->width(),"Light control exceeds narrow Inspector: "+field->objectName().toStdString());
            }
            for(const char* name:{"redEdit","greenEdit","blueEdit"}) {
                auto* field=panel->findChild<QLineEdit*>(name);
                Check(field&&field->width()>=32,"RGB field collapsed below usable width");
            }
        }
        auto* intensity=panel->findChild<QLineEdit*>("intensityEdit");auto* enabled=panel->findChild<QCheckBox*>("enabledCheck");
        Check(intensity&&enabled,"Designer light controls missing");intensity->setText("2.5");QMetaObject::invokeMethod(intensity,"editingFinished");
        Check(light->GetSettings().Intensity==2.5f,"Inspector value did not reach authoring transaction: "+error);
        auto* shadows=panel->findChild<QGroupBox*>("shadowGroup");auto* shadowDistance=panel->findChild<QLineEdit*>("shadowDistanceEdit");
        auto* shadowResolution=panel->findChild<QComboBox*>("shadowResolutionCombo");auto* shadowCascades=panel->findChild<QComboBox*>("shadowCascadesCombo");
        auto* shadowSoft=panel->findChild<QCheckBox*>("softShadowsCheck");
        auto* cacheStaticCasters=panel->findChild<QCheckBox*>("cacheStaticCastersCheck");
        Check(shadows&&shadowDistance&&shadowResolution&&shadowCascades&&shadowSoft&&cacheStaticCasters,"Designer shadow controls missing");
        Check(!cacheStaticCasters->isChecked(),"Static shadow caching was unexpectedly enabled by default");
        QTest::mouseClick(shadows,Qt::LeftButton,Qt::NoModifier,QPoint(8,12));
        Check(light->GetShadows().Enabled,"Shadow enable did not reach authoring transaction");
        shadowDistance->setText("75");QMetaObject::invokeMethod(shadowDistance,"editingFinished");
        shadowResolution->setCurrentIndex(2);shadowCascades->setCurrentIndex(1);
        QTest::mouseClick(shadowSoft,Qt::LeftButton,Qt::NoModifier,QPoint(8,shadowSoft->height()/2));
        Check(light->GetShadows().Distance==75&&light->GetShadows().Resolution==1024&&light->GetShadows().CascadeCount==2&&
            !light->GetShadows().Soft,"Inspector shadow values did not reach authoring transaction");
        Check(scenes.Undo(error)&&light->GetShadows().Soft,"Shadow edit Undo failed: "+error);
        Check(scenes.Redo(error)&&!light->GetShadows().Soft,"Shadow edit Redo failed: "+error);
        cacheStaticCasters->click();
        Check(light->GetShadows().CacheStaticCasters,"Inspector static shadow caching edit did not reach authoring transaction");
        Check(scenes.Undo(error)&&!light->GetShadows().CacheStaticCasters,"Static shadow caching Undo failed: "+error);
        Check(scenes.Redo(error)&&light->GetShadows().CacheStaticCasters,"Static shadow caching Redo failed: "+error);
        QTest::mouseClick(enabled,Qt::LeftButton,Qt::NoModifier,QPoint(8,enabled->height()/2));Check(!light->GetSettings().Enabled,"Inspector enabled toggle failed");
        delete panel;Check(scenes.SaveScene(error),error);const auto source=Read(temporary.path()+"/Assets/Light.scene");
        const auto objectFile=scenes.GetObjectFileID(object->GetInstanceID()),componentFile=scenes.GetComponentFileID(object->GetInstanceID(),light->GetComponentInstanceID());
        Check(scenes.BeginPlaySession(error),error);mode.Change(EditorPlayState::Playing);
        Check(lighting.ReadEnvironment(environment,error)&&environment.Available&&environment.Editable&&environment.Settings.Intensity==2,"Play did not clone authored environment");
        auto playEnvironment=environment.Settings;playEnvironment.Intensity=9;
        Check(lighting.SetEnvironment(environment.SceneID,playEnvironment,error)&&scenes.GetActiveScene()->GetEnvironmentLighting().Intensity==9,error);
        object=scenes.QueryObjectByFileID(objectFile);light=dynamic_cast<GameSproutRuntime::ILightComponent*>(scenes.QueryComponentByFileID(object->GetInstanceID(),componentFile));
        Check(light,"Play lost Light component identity");target={scenes.GetActiveScene()->GetInstanceID(),object->GetInstanceID(),light->GetComponentInstanceID()};
        value=light->GetSettings();value.Intensity=7;Check(lighting.SetLight(target,value,error),error);
        Check(!scenes.HasUnsavedChanges()&&Read(temporary.path()+"/Assets/Light.scene")==source,"Play Light edit wrote authoring file");
        Check(!lighting.CanCreate(target.SceneID,0,error),"Play permitted author Light creation");
        mode.Change(EditorPlayState::ExitingPlay);Check(scenes.EndPlaySession(error),error);mode.Change(EditorPlayState::Edit);
        Check(lighting.ReadEnvironment(environment,error)&&environment.Editable&&environment.Settings.Intensity==2,"Play environment leaked into author world");
        object=scenes.QueryObjectByFileID(objectFile);light=object->GetComponent<GameSproutRuntime::ILightComponent>();
        Check(light&&light->GetSettings().Intensity==2.5f,"Play Light edit leaked into author world");
        Check(scenes.OpenScene("Light.scene",error),error);object=scenes.GetActiveScene()->FindChild("Directional Light");
        Check(lighting.ReadEnvironment(environment,error)&&environment.Settings.Mode==GameSproutRuntime::AmbientLightMode::Gradient&&environment.Settings.SkyColor.R==2,"Saved scene lost environment");
        Check(object&&object->GetComponent<GameSproutRuntime::ILightComponent>()->GetSettings().Color.B==.8f,"Saved light round-trip failed");
        const auto savedShadows=object->GetComponent<GameSproutRuntime::ILightComponent>()->GetShadows();
        Check(savedShadows.Enabled&&savedShadows.Distance==75&&savedShadows.Resolution==1024&&savedShadows.CascadeCount==2&&
            !savedShadows.Soft&&savedShadows.CacheStaticCasters,
            "Saved light lost Inspector shadow settings");
        Json::StreamWriterBuilder environmentWriter;SceneDocumentSessionRequest prefabSession;
        prefabSession.DocumentJson=Json::writeString(environmentWriter,SceneDocument::CreateDefault("Environment Prefab Stage"));prefabSession.ContentRootFileID=1;
        Check(scenes.BeginDocumentSession(prefabSession,error),error);
        Check(lighting.ReadEnvironment(environment,error)&&!environment.Available&&!environment.Editable,"Prefab Stage exposed scene environment editing");
        Check(!lighting.SetEnvironment(scenes.GetActiveScene()->GetInstanceID(),environmentValue,error),"Prefab Stage accepted a scene environment transaction");
        Check(scenes.EndDocumentSession(error)&&lighting.ReadEnvironment(environment,error)&&environment.Available&&environment.Editable&&environment.Settings.SkyColor.R==2,
            "Returning from Prefab Stage lost source environment");
        const auto eventCountBeforeRuntime=environmentEvents.Events.size();
        auto runtimeEnvironment=scenes.GetActiveScene()->GetEnvironmentLighting();runtimeEnvironment.Mode=GameSproutRuntime::AmbientLightMode::Color;
        Check(scenes.GetActiveScene()->SetEnvironmentLighting(runtimeEnvironment),"Runtime environment mutation failed");runtime->ProcessPendingEvents();
        Check(environmentEvents.Events.size()>eventCountBeforeRuntime&&environmentEvents.Events.back().Settings.Mode==GameSproutRuntime::AmbientLightMode::Color,
            "Runtime environment event did not reach authoring subscribers");
        GameSproutEditor::GameSproutEditorFinal::Stop(view,editor);Check(hierarchy.Unregistered&&inspector.Unregistered,"Light View leaked extension callbacks");
        Check(ide.Bar.Item.Callback==nullptr,"Environment window menu callback survived Release");
        lighting.UnregisterEnvironmentListener(&environmentEvents);const auto environmentEventCount=environmentEvents.Events.size();
        auto directEnvironment=scenes.GetActiveScene()->GetEnvironmentLighting();directEnvironment.Mode=GameSproutRuntime::AmbientLightMode::Gradient;
        Check(scenes.GetActiveScene()->SetEnvironmentLighting(directEnvironment),"Direct Runtime environment update failed");runtime->ProcessPendingEvents();
        Check(environmentEvents.Events.size()==environmentEventCount,"Unregistered environment listener was called");
        GameSproutEditor::GameSproutEditorFinal::Stop(lighting,api);GameSproutEditor::GameSproutEditorFinal::Stop(objects,api);GameSproutEditor::GameSproutEditorFinal::Stop(scenes,api);
        delete runtime;runtime=nullptr;std::cout<<"PASS Light creation, undo/redo, Inspector, save/open and transient Play edits\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}

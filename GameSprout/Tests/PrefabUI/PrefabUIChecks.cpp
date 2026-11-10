#include "PrefabUI.h"
#include "IIDEWindow.h"
#include <QToolButton>
#include "IAssetsPipeline.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <iostream>
#include <map>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutView& view,GameSproutViewApi& api){return view.Initialize(&api);}
    static bool Launch(IGameSproutView& view,GameSproutViewApi& api){return view.Launch(&api);}
    static void Tick(IGameSproutView& view,GameSproutViewApi& api){view.Update(&api);}
    static void Release(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
};
}
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void Pump(){for(int i=0;i<4;++i)QCoreApplication::processEvents();}
template<class T>T* Find(QWidget* widget,const char* name){auto* result=widget->findChild<T*>(name);Check(result,std::string("Designer control missing: ")+name);return result;}
template<class T>class Module : public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
template<class T>class View : public T {
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
class Scene final : public GameSproutRuntime::IScene {
public:
    std::string Name="Scene";
    uint64_t GetInstanceID() const override{return 10;}
    const std::string& GetName() const override{return Name;}
    void SetName(const std::string&) override{}
    std::uint8_t GetLayer() const override{return Layer;}
    bool SetLayer(std::uint8_t value) override{if(value>=32)return false;Layer=value;return true;}
    std::uint64_t GetTagID() const override{return 0;}
    const std::string& GetTag() const override{return Tag;}
    bool SetTagID(std::uint64_t value) override{return value==0;}
    bool SetTag(const std::string& value) override{return value==Tag;}
    bool CompareTag(const std::string& value) const override{return value==Tag;}
    GameSproutRuntime::StaticFlags GetStaticFlags() const override{return Static;}
    bool SetStaticFlags(GameSproutRuntime::StaticFlags value) override{
        if(!GameSproutRuntime::IsValidStaticFlags(value))return false;Static=value;return true;
    }
    std::uint8_t Layer=0;
    std::string Tag="Untagged";
    GameSproutRuntime::StaticFlags Static=GameSproutRuntime::StaticFlags::None;

    IScene* GetScene() const override{return const_cast<Scene*>(this);}
    IGameObject* Parent() const override{return nullptr;}
    bool SetParent(IGameObject*,bool) override{return false;}
    size_t GetChildCount() const override{return 0;}
    IGameObject* GetChild(size_t) const override{return nullptr;}
    IGameObject* FindChild(const std::string&) const override{return nullptr;}
    void ForeachChild(const fForeachChildCallback&) const override{}
    bool AddChild(IGameObject*,bool) override{return false;}
    bool RemoveChild(IGameObject*,bool) override{return false;}
    bool SetSiblingIndex(size_t) override{return false;}
    void SetActive(bool) override{}
    bool IsActiveSelf() const override{return true;}
    bool IsActiveInHierarchy() const override{return true;}
    GameSproutRuntime::ITransformComponent* GetTransform() const override{return nullptr;}
    size_t GetComponentCount() const override{return 0;}
    GameSproutRuntime::IComponent* GetComponentAt(size_t) const override{return nullptr;}
    void ForeachComponent(const fForeachComponentCallback&) const override{}
    GameSproutRuntime::IComponent* AddComponent(const std::type_info&) override{return nullptr;}
    GameSproutRuntime::IComponent* GetComponent(const std::type_info&) const override{return nullptr;}
    bool RemoveComponent(const std::type_info&) override{return false;}
    IGameObject* CreateGameObject(const std::string&,IGameObject*) override{return nullptr;}
    bool DestroyGameObject(IGameObject*) override{return false;}
    bool SetMainCamera(GameSproutRuntime::ICameraComponent*) override{return false;}
    GameSproutRuntime::ICameraComponent* GetMainCamera() const override{return nullptr;}
    GameSproutRuntime::EnvironmentLightingSettings Environment;
    const GameSproutRuntime::EnvironmentLightingSettings& GetEnvironmentLighting() const override{return Environment;}
    bool SetEnvironmentLighting(const GameSproutRuntime::EnvironmentLightingSettings& value) override{if(!GameSproutRuntime::IsValidEnvironmentLighting(value))return false;Environment=value;return true;}
};
class Scenes final : public Module<ISceneManager> {
public:
    Scene Active;
    bool Playing=false;
    ISceneListener* Listener=nullptr;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene() const override{return const_cast<Scene*>(&Active);}
    const std::string& GetActiveScenePath() const override{return Path;}
    bool OpenScene(const std::string&,std::string&) override{return false;}
    bool IsPlaySessionActive() const override{return Playing;}
    bool EditObject(uint64_t,const std::string&,const fSceneEditOperation&,const fSceneEditOperation&,std::string&) override{return false;}
    bool Undo(std::string&) override{return false;}
    bool Redo(std::string&) override{return false;}
    bool CanUndo() const override{return false;}
    bool CanRedo() const override{return false;}
    bool SaveScene(std::string&) override{return false;}
    bool SaveSceneAs(const std::string&,std::string&) override{return false;}
    bool HasUnsavedChanges() const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool RegisterSceneListener(ISceneListener* listener) override{Listener=listener;return true;}
    void UnregisterSceneListener(ISceneListener*) override{Listener=nullptr;}
};
class Selection final : public Module<ISelectionManager> {
public:
    mutable int Reads=0;
    SceneObjectSelection Object{10,100};
    std::vector<ProjectAssetSelection> Assets;
    ISelectionListener* Listener=nullptr;
    std::vector<ProjectAssetSelection> GetAssetSelection() const override{++Reads;return Assets;}
    SceneObjectSelection GetSceneObjectSelection() const override{++Reads;return Object;}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>& value) override{Assets=value;Object={};if(Listener)Listener->OnAssetSelectionChanged(value);}
    void SetSceneObjectSelection(const SceneObjectSelection& value) override{Object=value;Assets.clear();if(Listener)Listener->OnSceneObjectSelectionChanged(value);}
    bool RegisterSelectionListener(ISelectionListener* listener) override{Listener=listener;return true;}
    void UnregisterSelectionListener(ISelectionListener*) override{Listener=nullptr;}
};
class Prefabs final : public Module<IPrefabManager> {
public:
    mutable int Reads=0;
    int Instantiates=0,Applies=0,Reverts=0,Saves=0,Creates=0,DirectoryCreates=0;
    uint64_t LastParent=0;
    uint64_t LastObject=0;
    GameSproutRuntime::ResourceReference LastSource;
    std::string SavedPath;
    bool Missing=false,FailSave=false;
    IPrefabListener* Listener=nullptr;
    bool SavePrefab(uint64_t id,const std::string& path,std::string& error) override{++Saves;LastObject=id;SavedPath=path;if(FailSave){error="目标已存在，拒绝覆盖。";return false;}return true;}
    bool CreatePrefab(uint64_t id,const std::string& path,std::string& error) override{++Creates;LastObject=id;SavedPath=path;if(FailSave){error="目标已存在，拒绝覆盖。";return false;}return true;}
    bool CreatePrefabInDirectory(uint64_t id,const std::string& directory,std::string& path,std::string&) override{++DirectoryCreates;LastObject=id;path=directory+"/Hero 1.prefab";SavedPath=path;return true;}
    bool InstantiatePrefab(const GameSproutRuntime::ResourceReference& source,uint64_t parent,uint64_t& id,std::string& error) override{
        LastParent=parent;++Instantiates;LastSource=source;
        if(!source.LocalID){error="Prefab 尚未完成导入。";return false;}id=300;return true;
    }
    bool ReadInstance(uint64_t id,PrefabInstanceInfo& info,std::string& error) const override{
        ++Reads;if(id!=100&&id!=101&&id!=200){error="当前对象不是 Prefab 实例。";return false;}
        info.RootObjectID=id==101?100:id;info.Source={"12345678901234567890123456789012",7865};info.SourcePath="Prefabs/Hero_Rig_With_A_Long_Descriptive_Source_Name.prefab";
        info.HasOverrides=true;info.MissingSource=Missing;if(Missing)info.Error="Prefab 源文件已移除。";
        PrefabOverrideInfo item;item.Kind=PrefabOverrideKind::Property;item.SourceObjectID=1;item.ComponentID=2;item.PropertyPath="Position";item.ObjectName="Hero";item.ComponentType="Transform";item.SourceValueJson="[0,0,0]";item.InstanceValueJson="[1,0,0]";info.Overrides.push_back(item);return true;
    }
    bool ApplyOverrides(uint64_t id,std::string&) override{++Applies;LastObject=id;return true;}
    bool RevertOverrides(uint64_t id,std::string&) override{++Reverts;LastObject=id;return true;}
    bool RefreshSource(const std::string&,std::string&) override{return true;}
    bool RegisterPrefabListener(IPrefabListener* listener) override{Listener=listener;return true;}
    void UnregisterPrefabListener(IPrefabListener*) override{Listener=nullptr;}
};
class Asset final : public IAsset {
public:
    Guid ID{"12345678901234567890123456789012"};
    std::string Text;
    const Guid& GetGuid() const override{return ID;}
    uint64_t GetLocalID() const override{return 7865;}
    eAssetType GetAssetType() const override{return eAssetType::Prefab;}
    const std::string& GetName() const override{return Text;}
    const std::string& GetArtifactPath() const override{return Text;}
    const std::string& GetDataDescriptorJson() const override{return Text;}
    uint32_t GetDataVersion() const override{return 1;}
    uint64_t GetDataSize() const override{return 0;}
};
class Meta final : public IAssetMeta {
public:
    Meta():IAssetMeta(eAssetImporterType::Prefab,Guid("12345678901234567890123456789012")){}
    Asset Resource;
    bool Published=true;
    std::string Path="Prefabs/Test.prefab";
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    // Main resources are deliberately absent: the View must use the manifest.
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return Published?eAssetImportState::Ready:eAssetImportState::Pending;}
    std::string GetImportError() const override{return {};}
    bool SerializeMeta(std::string&) const override{return false;}
    bool SetSubAssets(const std::string&) override{return false;}
    bool SaveMeta() override{return false;}
};
class Assets final : public Module<IAssetsManager> {
public:
    Meta Value;
    IAssetListener* Listener=nullptr;
    IAssetMeta* QueryAsset(const Guid& guid) const override{return guid==Value._Guid?const_cast<Meta*>(&Value):nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener* listener) override{Listener=listener;return true;}
    void UnregisterAssetListener(IAssetListener*) override{Listener=nullptr;}
};
class Library final : public Module<ILibraryManager> {
public:
    Meta* Source=nullptr;
    std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {};}
    bool GetManifest(const Guid& guid,AssetImportManifest& manifest) const override {
        if(!Source||!Source->Published||guid!=Source->_Guid)return false;
        AssetArtifact main;main.LocalID=Source->Resource.GetLocalID();main.Type=eAssetType::Prefab;main.SourceKey="main";
        AssetArtifact auxiliary=main;auxiliary.LocalID=9999;auxiliary.SourceKey="auxiliary";
        manifest.Assets={auxiliary,main};return true;
    }
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid& guid,uint64_t id) override{return Source&&Source->Published&&guid==Source->_Guid&&id==Source->Resource.GetLocalID()?&Source->Resource:nullptr;}
};
class PlayMode final : public Module<IPlayMode> {
public:
    PlayModeState State;
    IPlayModeListener* Listener=nullptr;
    PlayModeState GetState() const override{return State;}
    bool Play(std::string&) override{return false;}
    bool Pause(std::string&) override{return false;}
    bool Resume(std::string&) override{return false;}
    bool Step(std::string&) override{return false;}
    bool Stop(std::string&) override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener* listener) override{Listener=listener;return true;}
    void UnregisterPlayModeListener(IPlayModeListener*) override{Listener=nullptr;}
    void Change(EditorPlayState state){State.State=state;if(Listener)Listener->OnPlayModeStateChanged(State);}
};
class IDE final : public View<IIDEWindow> {
public:
    int Creates=0;
    IIDEMenuBar* GetMenuBar() const override{return nullptr;}
    IIDEDocker* CreateDocker(const QString&) override{++Creates;return nullptr;}
    IIDEDocker* QueryDocker(const QString&) override{return nullptr;}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation) override{return true;}
    void Loading(const std::string&,const std::string&) override{}
    void Unloading() override{}
    void SetLoadingInfo(const std::string&,const std::string&) override{}
};
class Inspector final : public View<IInspectorUI> {
public:
    QWidget Host;
    IInspectorExtension* Extension=nullptr;
    InspectorSection* Section=nullptr;
    InspectorServices Services;
    InspectorTarget Target;
    bool Locked=false;
    bool HasPendingChanges() const override{return false;}
    bool ApplyChanges() override{return false;}
    void RevertChanges() override{}
    bool RegisterExtension(IInspectorExtension* value) override{Extension=value;return true;}
    void UnregisterExtension(IInspectorExtension* value) override{if(Extension==value){if(Section){Section->Deactivate();delete Section;}Section=nullptr;Extension=nullptr;}}
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*) override{return nullptr;}
    void Refresh(){
        if(!Section)return;
        std::string error;Section->setVisible(Section->Load(Target,false,error));
    }
    void Inspect(const InspectorTarget& target){
        Target=target;
        if(!Section){Section=Extension->CreateSceneObjectSection(Services,&Host);
            Section->RequestRefresh=[this]{Refresh();};
            Section->RequestSelection=[this](const ProjectAssetSelection& value){Services.Selection->SetAssetSelection({value});if(!Locked){Target={};Section->Deactivate();Section->hide();}};
        }
        if(!target.Scene.ObjectID){Section->Deactivate();Section->hide();return;}Refresh();
    }
};
class Hierarchy final : public View<IHierarchyUI> {
public:
    HierarchyActionContext Context{10,100};
    fContextActionCallback Action=nullptr;
    fContextActionEnabled Enabled;
    fAssetDropProbe Probe;
    fAssetDropExecute Execute;
    uint64_t GetSelectedObjectID() const override{return 200;}
    bool RegisterAssetDropHandler(const void*,const fAssetDropProbe& probe,const fAssetDropExecute& execute) override{Probe=probe;Execute=execute;return true;}
    bool RegisterContextAction(const void*,const std::vector<std::string>&,fContextActionCallback action,const fContextActionEnabled& enabled) override{Action=action;Enabled=enabled;return true;}
    HierarchyActionContext GetActionContext() const override{return Context;}
    void UnregisterExtensions(const void*) override{Action=nullptr;Enabled={};Probe={};Execute={};}
};
class Scripts final : public Module<IScriptEditor> {
public:
    std::vector<EditorScriptType> GetScriptTypes() const override{return {};}
    ScriptBuildState GetBuildState() const override{return {};}
    bool RegisterScriptListener(IScriptEditorListener*) override{return true;}
    void UnregisterScriptListener(IScriptEditorListener*) override{}
    bool CreateScript(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool RequestCompile(std::string&) override{return false;}
    bool ReadObjectTarget(uint64_t id,ScriptObjectTarget& target,std::string&) const override{target={77,10,id};return id==100||id==200;}
};
class ProjectAssets final : public View<IProjectAssetsUI> {
public:
    fAssetOpenHandler Open;
    fDropProbe Probe;
    fDropExecute Execute;
    bool RegisterDropHandler(const void*,const fDropProbe& probe,const fDropExecute& execute) override{Probe=probe;Execute=execute;return true;}
    fContextActionEnabled Enabled;
    fContextActionCallback Action=nullptr;
    ProjectAssetActionContext Context;
    std::string Created;
    std::vector<ProjectAssetSelection> GetSelection() const override{return Context.Selection;}
    bool NavigateToDirectory(const std::string&) override{return true;}
    bool RegisterAssetOpenHandler(const void*,const fAssetOpenHandler& callback) override{Open=callback;return true;}
    bool RegisterContextAction(const void*,const std::vector<std::string>& path,fContextActionCallback action,const fContextActionEnabled& enabled) override{
        Check(path.size()==2&&path[0]=="Prefab","Context menu path is not Designer-defined");Action=action;Enabled=enabled;return true;
    }
    void UnregisterExtensions(const void*) override{Open={};Enabled={};Action=nullptr;Probe={};Execute={};}
    ProjectAssetActionContext GetActionContext() const override{return Context;}
    void NotifyAssetCreated(const std::string& path) override{Created=path;}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    Editor():GameSproutViewApi(nullptr){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{auto i=Modules.find(name);return i==Modules.end()?nullptr:i->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{auto i=Views.find(name);return i==Views.end()?nullptr:i->second;}
};
}
int main(int argc,char** argv){QApplication application(argc,argv);try {
    Scenes scenes;Selection selection;Prefabs prefabs;Assets assets;Library library;PlayMode play;IDE ide;ProjectAssets project;Inspector inspector;Hierarchy hierarchy;Scripts scripts;Editor editor;
    library.Source=&assets.Value;
    editor.Modules={{"SceneManager",&scenes},{"SelectionManager",&selection},{"PrefabManager",&prefabs},{"AssetsManager",&assets},{"LibraryManager",&library},{"PlayMode",&play},{"ScriptEditor",&scripts}};
    editor.Views={{"IDEWindow",&ide},{"ProjectAssetsUI",&project},{"InspectorUI",&inspector},{"HierarchyUI",&hierarchy}};
    inspector.Services={&editor,&assets,&library,&selection,&scenes};
    PrefabUI view;GameSproutEditor::GameSproutEditorFinal::Before(view,editor);
    Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,editor),"Initialize Prefab View");
    Check(GameSproutEditor::GameSproutEditorFinal::Launch(view,editor),"Launch Prefab routes");
    Check(ide.Creates==0&&inspector.Extension==&view,"Prefab must register an additive Inspector extension without creating a dock");
    InspectorTarget root;root.Scene={10,100};
    Check(!view.CanInspect(inspector.Services,root),"Prefab stole the scene object component page");
    inspector.Inspect(root);auto* section=inspector.Section;Check(section&&!section->isHidden(),"Missing Prefab instance section");
    auto* overrides=Find<QToolButton>(section,"overridesButton");auto* select=Find<QToolButton>(section,"selectSourceButton");
    auto* popup=Find<QDialog>(section,"PrefabOverridesPopup");auto* apply=Find<QPushButton>(popup,"applyAllButton");auto* revert=Find<QPushButton>(popup,"revertAllButton");
    Check(Find<QTreeWidget>(popup,"overridesTree")->topLevelItemCount()==0,"Hidden override popup eagerly allocated rows");
    overrides->click();
    Check(apply->isEnabled()&&Find<QTreeWidget>(popup,"overridesTree")->topLevelItemCount()==1,"Missing initial override snapshot");
    auto* overrideRow=Find<QTreeWidget>(popup,"overridesTree")->topLevelItem(0);
    Check(overrideRow->text(1)=="Hero / Transform"&&overrideRow->text(2)=="Position"&&overrideRow->toolTip(1).contains("SourceObjectID"),"Override list does not expose readable object/component names");
    popup->hide();
    inspector.Locked=true;selection.SetSceneObjectSelection({10,200});apply->click();revert->click();
    Check(prefabs.Applies==1&&prefabs.Reverts==1&&prefabs.LastObject==100,"Apply/Revert followed global selection instead of the locked Inspector target");Pump();
    select->click();Check(inspector.Locked&&inspector.Target.Scene.ObjectID==100&&selection.Assets.size()==1,"Select source changed the locked target");
    const int reads=prefabs.Reads,selectionReads=selection.Reads;
    for(int i=0;i<120;++i)GameSproutEditor::GameSproutEditorFinal::Tick(view,editor);Pump();
    Check(prefabs.Reads==reads&&selection.Reads==selectionReads,"Prefab UI polls while idle");
    for(int i=0;i<20;++i)prefabs.Listener->OnPrefabStateChanged();Pump();Check(prefabs.Reads==reads+1,"Prefab events did not coalesce");
    root.Scene.ObjectID=101;inspector.Inspect(root);Check(overrides->isHidden(),"Descendant presented root-wide Overrides as local operations");
    root.Scene.ObjectID=100;inspector.Inspect(root);prefabs.Missing=true;prefabs.Listener->OnPrefabStateChanged();Pump();
    Check(!apply->isEnabled()&&!revert->isEnabled()&&Find<QLabel>(section,"stateLabel")->text().contains("MissingSource"),"MissingSource author barrier missing");
    prefabs.Missing=false;prefabs.Listener->OnPrefabStateChanged();Pump();
    play.Change(EditorPlayState::EnteringPlay);apply->click();Check(prefabs.Applies==1,"Play transition allowed authoring before refresh");Pump();
    Check(!apply->isEnabled()&&!revert->isEnabled(),"Play author buttons remained enabled");play.Change(EditorPlayState::Edit);Pump();
    // Normal objects contribute no visible section or empty height.
    root.Scene.ObjectID=999;inspector.Inspect(root);Check(section->isHidden(),"Ordinary object leaves an empty Prefab section");
    root.Scene.ObjectID=100;inspector.Inspect(root);overrides->click();Check(popup->isVisible(),"Overrides popup did not open");
    inspector.Inspect({});Check(section->isHidden()&&!popup->isVisible(),"Deactivate failed to close Overrides when clearing/switching to an asset");
    inspector.Inspect(root);
    if(qEnvironmentVariableIsSet("GAMESPROUT_PREFAB_UI_CAPTURE")) {
        section->resize(280,56);section->show();inspector.Host.resize(280,70);inspector.Host.show();Pump();
        auto* sourceLabel=Find<QLabel>(section,"sourceLabel");
        Check(sourceLabel->text().contains(QChar(0x2026))&&sourceLabel->toolTip().contains("Hero_Rig_With_A_Long_Descriptive_Source_Name.prefab"),"Narrow source label must elide and retain its complete path tooltip");
        Check(section->grab().save(qEnvironmentVariable("GAMESPROUT_PREFAB_UI_CAPTURE")),"Capture narrow Inspector header");
        section->resize(360,56);inspector.Host.resize(360,70);Pump();
        Check(section->grab().save("/tmp/gamesprout-prefab-inspector-360.png"),"Capture 360px Inspector header");
        overrides->click();Pump();Check(popup->grab().save("/tmp/gamesprout-prefab-overrides-popup.png"),"Capture Overrides popup");popup->hide();
    }
    selection.SetSceneObjectSelection({10,200});project.Context.Directory="Prefabs";hierarchy.Action();Pump();
    QDialog* saveDialog=nullptr;for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()=="PrefabSaveDialog")saveDialog=qobject_cast<QDialog*>(widget);
    Check(saveDialog,"Hierarchy context failed to open save dialog");Find<QLineEdit>(saveDialog,"nameEdit")->setText("Hero");prefabs.FailSave=true;
    Find<QDialogButtonBox>(saveDialog,"buttonBox")->button(QDialogButtonBox::Save)->click();
    Check(prefabs.Creates==1&&prefabs.Saves==0&&saveDialog->isVisible()&&Find<QLabel>(saveDialog,"errorLabel")->text().contains("拒绝覆盖"),"Create conflict failed to preserve draft or used export-only SavePrefab");
    prefabs.FailSave=false;Find<QDialogButtonBox>(saveDialog,"buttonBox")->button(QDialogButtonBox::Save)->click();
    Check(prefabs.Creates==2&&prefabs.LastObject==100&&project.Created=="Prefabs/Hero.prefab","Context save used global selection or lost directory/name");Pump();
    std::string error;SceneObjectDropContext objectDrop{"Prefabs",77,10,100};
    Check(project.Probe(objectDrop)&&project.Execute(objectDrop,error)&&prefabs.DirectoryCreates==1&&project.Created=="Prefabs/Hero 1.prefab","Hierarchy to Project drop failed direct unique creation");
    objectDrop.SessionID=76;Check(!project.Probe(objectDrop)&&!project.Execute(objectDrop,error)&&prefabs.DirectoryCreates==1,"Stale dragged object session was accepted");
    const ProjectAssetSelection asset{"12345678901234567890123456789012",0,false};project.Context.Selection={asset};
    HierarchyAssetDropContext assetDrop{10,200,{asset}};Check(hierarchy.Probe(assetDrop)&&hierarchy.Execute(assetDrop,error)&&prefabs.LastParent==200&&prefabs.LastSource.LocalID==7865,"Project drop lost target parent or guessed main LocalID");
    Check(selection.Object.ObjectID==300,"Instantiation did not select new instance");
    const int instantiates=prefabs.Instantiates;Check(!project.Open&&prefabs.Instantiates==instantiates,"PrefabUI must leave double-click ownership to PrefabStageUI");
    InspectorTarget resource;resource.Assets={asset};Check(view.CanInspect(inspector.Services,resource),"Prefab asset lacks scope explanation Inspector");
    auto* assetPanel=view.CreatePanel(inspector.Services,&inspector.Host);Check(assetPanel->Load(resource,true,error)&&Find<QLabel>(assetPanel,"scopeLabel")->text().contains("Prefab Stage"),"Prefab asset Inspector lacks Stage entry guidance");delete assetPanel;
    project.Action();Check(prefabs.Instantiates==instantiates+1&&prefabs.LastParent==0,"Explicit Project menu no longer instantiates");
    play.Change(EditorPlayState::Paused);Check(!hierarchy.Probe(assetDrop)&&!project.Probe({"Prefabs",77,10,100})&&!project.Enabled(project.Context),"Paused state accepted authoring routes");
    play.Change(EditorPlayState::Edit);Pump();
    prefabs.Listener->OnPrefabStateChanged();GameSproutEditor::GameSproutEditorFinal::Release(view,editor);Pump();
    Check(!inspector.Extension&&!inspector.Section&&!prefabs.Listener&&!assets.Listener&&!scenes.Listener&&!play.Listener&&!project.Open&&!project.Action&&!project.Probe&&!hierarchy.Action&&!hierarchy.Probe,"Release retained section callbacks or routes");
    Check(ide.Creates==0,"Prefab created an independent panel");
    std::cout<<"PASS Prefab Inspector sections: locked targets, root Overrides, source selection, event snapshots, no dock, create/drop routes, Stage scope and lifecycle\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

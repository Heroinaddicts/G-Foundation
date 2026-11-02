#pragma once
#include "PrefabStageUI.h"
#include "SceneEditorUI.h"
#include "IAssetsManager.h"
#include "IIDEWindow.h"
#include "IHierarchyUI.h"
#include "IDEWindow.h"
#include <QApplication>
#include <QDialog>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QVBoxLayout>
#include <map>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutView& view,GameSproutViewApi& api){return view.Initialize(&api);}
    static bool InitializeBare(IGameSproutView& view){return view.Initialize(nullptr);}
    static bool Launch(IGameSproutView& view,GameSproutViewApi& api){return view.Launch(&api);}
    static void Tick(IGameSproutView& view,GameSproutViewApi& api){view.Update(&api);}
    static void Release(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
};
}
inline void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
inline void Pump(){for(int i=0;i<4;++i)QCoreApplication::processEvents();}
template<class T>T* Find(QWidget* widget,const char* name){auto* value=widget->findChild<T*>(name);Check(value,std::string("Missing .ui control: ")+name);return value;}
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
    std::string Name="Scene";uint64_t ID=10;
    uint64_t GetInstanceID() const override{return ID;}
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
};

class Scenes final : public Module<ISceneManager> {
public:
    Scene Active;std::string Path="Main.scene";bool Dirty=true,Document=false;
    int Saves=0,SaveAs=0,Opens=0;std::vector<ISceneListener*> Listeners;
    GameSproutRuntime::IScene* GetActiveScene() const override{return const_cast<Scene*>(&Active);}
    const std::string& GetActiveScenePath() const override{return Path;}
    bool IsDocumentSessionActive() const override{return Document;}
    bool OpenScene(const std::string& path,std::string&) override{++Opens;Path=path;Dirty=false;return true;}
    bool EditObject(uint64_t,const std::string&,const fSceneEditOperation&,const fSceneEditOperation&,std::string&) override{return false;}
    bool Undo(std::string&) override{return false;}
    bool Redo(std::string&) override{return false;}
    bool CanUndo() const override{return true;}
    bool CanRedo() const override{return false;}
    bool SaveScene(std::string&) override{++Saves;Dirty=false;return true;}
    bool SaveSceneAs(const std::string&,std::string&) override{++SaveAs;return true;}
    bool HasUnsavedChanges() const override{return Dirty;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool RegisterSceneListener(ISceneListener* value) override{Listeners.push_back(value);return true;}
    void UnregisterSceneListener(ISceneListener* value) override{std::erase(Listeners,value);}
};
class Inspector final : public View<IInspectorUI> {
public:
    InspectorViewState State{true,{10,100},{}};
    SceneObjectSelection GlobalSelection{10,200};
    mutable int Captures=0;int Restores=0;
    bool HasPendingChanges() const override{return false;}
    bool ApplyChanges() override{return false;}
    void RevertChanges() override{}
    bool RegisterExtension(IInspectorExtension*) override{return true;}
    void UnregisterExtension(IInspectorExtension*) override{}
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*) override{return nullptr;}
    InspectorViewState CaptureViewState() const override{++Captures;return State;}
    void RestoreViewState(const InspectorViewState& value) override{State=value;++Restores;}
};
class Stage final : public Module<IPrefabStage> {
public:
    PrefabStageState State;mutable int Reads=0;int Opens=0,Saves=0,Closes=0;bool FailSave=false,RevealDirtyOnClose=false,InlineCallback=false;Scenes* World=nullptr;
    std::vector<IPrefabStageListener*> Listeners;Inspector* InspectorView=nullptr;
    PrefabStageState GetState() const override{++Reads;return State;}
    void Emit(){const auto listeners=Listeners;for(auto* value:listeners)value->OnPrefabStageChanged(State);}
    bool OpenPrefab(const std::string& guid,std::string&) override{Check(!InlineCallback,"Opening Stage destroyed a world inside the caller callback");++Opens;State.Active=true;State.Guid=guid;State.Path="Hero.prefab";State.SceneID=20;State.ContentRootObjectID=21;World->Document=true;World->Active.ID=20;if(InspectorView)InspectorView->GlobalSelection={20,21};Emit();return true;}
    bool Save(std::string& error) override{++Saves;if(FailSave){error="Source changed externally";return false;}State.Dirty=false;Emit();return true;}
    bool Close(bool discard,std::string& error) override{Check(!InlineCallback,"Closing Stage destroyed a world inside the caller callback");if(RevealDirtyOnClose){State.Dirty=true;RevealDirtyOnClose=false;Emit();}if(State.Dirty&&!discard){error="Dirty stage";return false;}++Closes;State={};World->Document=false;World->Active.ID=10;if(InspectorView)InspectorView->GlobalSelection={10,200};Emit();return true;}
    bool RegisterStageListener(IPrefabStageListener* value) override{Listeners.push_back(value);return true;}
    void UnregisterStageListener(IPrefabStageListener* value) override{std::erase(Listeners,value);}
};
class Meta final : public IAssetMeta {
public:
    std::string Path="Hero.prefab";
    Meta():IAssetMeta(eAssetImporterType::Prefab,Guid("0123456789abcdef0123456789abcdef")){}
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return eAssetImportState::Ready;}
    std::string GetImportError() const override{return {};}
    bool SerializeMeta(std::string&) const override{return false;}
    bool SetSubAssets(const std::string&) override{return false;}
    bool SaveMeta() override{return false;}
};
class Assets final : public Module<IAssetsManager> {
public:
    Meta Source;
    IAssetMeta* QueryAsset(const Guid& guid) const override{return guid==Source._Guid?const_cast<Meta*>(&Source):nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener*) override{return true;}
    void UnregisterAssetListener(IAssetListener*) override{}
};
class Browser final : public View<IProjectAssetsUI> {
public:
    std::map<const void*,fAssetOpenHandler> Open;
    std::vector<ProjectAssetSelection> GetSelection() const override{return {};}
    bool NavigateToDirectory(const std::string&) override{return true;}
    bool RegisterAssetOpenHandler(const void* owner,const fAssetOpenHandler& value) override{Open[owner]=value;return true;}
    bool RegisterContextAction(const void*,const std::vector<std::string>&,fContextActionCallback,const fContextActionEnabled&) override{return true;}
    void UnregisterExtensions(const void* owner) override{Open.erase(owner);}
    ProjectAssetActionContext GetActionContext() const override{return {};}
    void NotifyAssetCreated(const std::string&) override{}
};
class Menu final : public IIDEMenu,public IIDEMenuBar {
public:
    std::map<QString,Menu*> Children;fMenuClickCallback Click=nullptr;bool Enabled=true;
    ~Menu() override{for(auto& value:Children)delete value.second;}
    IIDEMenu* CreateChildMenu(const QString& name) override{auto& child=Children[name];if(!child)child=new Menu();return child;}
    void SetClickCallback(fMenuClickCallback value) override{Click=value;}
    void SetEnabled(bool value) override{Enabled=value;}
};
class IDE final : public View<IIDEWindow> {
public:
    Menu Bar;QWidget* Toolbar=nullptr;int DockCreates=0;IDEWindow Host;
    IIDEMenuBar* GetMenuBar() const override{return const_cast<Menu*>(&Bar);}
    bool AttachToolbarWidget(QWidget* value) override{Check(GameSproutEditor::GameSproutEditorFinal::InitializeBare(Host),"IDE host initialization failed");if(!Host.AttachToolbarWidget(value))return false;Toolbar=value;Host.Show();return true;}
    bool SetToolbarWidgetVisible(QWidget* value,bool visible) override{return Host.SetToolbarWidgetVisible(value,visible);}
    bool RemoveToolbarWidget(QWidget* value) override{Check(value==Toolbar,"Wrong toolbar removed");const bool removed=Host.RemoveToolbarWidget(value);if(removed)Toolbar=nullptr;return removed;}
    QToolBar* Shell() const{return Toolbar?qobject_cast<QToolBar*>(Toolbar->parentWidget()):nullptr;}
    QAction* ToolbarAction() const{if(auto* shell=Shell())for(auto* action:shell->actions())if(shell->widgetForAction(action)==Toolbar)return action;return nullptr;}
    IIDEDocker* CreateDocker(const QString&) override{++DockCreates;return nullptr;}
    IIDEDocker* QueryDocker(const QString&) override{return nullptr;}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation) override{return false;}
    void Loading(const std::string&,const std::string&) override{}
    void Unloading() override{}
    void SetLoadingInfo(const std::string&,const std::string&) override{}
};
class SceneView final : public View<ISceneViewUI> {
public:
    SceneViewCameraState Camera;int Focuses=0,Restores=0;
    SceneViewCameraState GetCameraState() const override{return Camera;}
    bool SetCameraState(const SceneViewCameraState& value) override{Camera=value;++Restores;return true;}
    void ResetCamera() override{}
    bool FocusSelection() override{++Focuses;Camera.Position={1,2,3};return true;}
    bool IsRenderingAvailable() const override{return false;}
    std::string GetRenderError() const override{return {};}
};
class Hierarchy final : public View<IHierarchyUI> {
public:
    QWidget Window;QWidget* Header=nullptr;
    Hierarchy(){Window.resize(300,120);new QVBoxLayout(&Window);Window.show();}
    uint64_t GetSelectedObjectID() const override{return 0;}
    bool AttachHeaderWidget(QWidget* value) override{if(!value||Header)return false;Header=value;Window.layout()->addWidget(value);value->show();return true;}
    bool SetHeaderWidgetVisible(QWidget* value,bool visible) override{if(value!=Header)return false;value->setVisible(visible);return true;}
    bool RemoveHeaderWidget(QWidget* value) override{if(value!=Header)return false;delete Header;Header=nullptr;return true;}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    Editor():GameSproutViewApi(nullptr){}
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{const auto it=Views.find(name);return it==Views.end()?nullptr:it->second;}
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto it=Modules.find(name);return it==Modules.end()?nullptr:it->second;}
};
inline QDialog* Dialog(const char* name){for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()==name&&widget->isVisible())return qobject_cast<QDialog*>(widget);return nullptr;}

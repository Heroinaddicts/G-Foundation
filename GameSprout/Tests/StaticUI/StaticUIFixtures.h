#pragma once
#include "StaticUI.h"
#include "InspectorUI.h"
#include "SceneObjectUI.h"
#include "SceneManager.h"
#include "IStaticAuthoring.h"
IStaticAuthoring* CreateProductionAuthoring();
#include "IProject.h"
#include "IIDEWindow.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "ObjectEditing.h"
#include <QApplication>
#include <QPointer>
#include <map>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static bool Start(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);return view.Initialize(&api)&&view.Launch(&api);}
    static void Finished(IGameSproutView& view,GameSproutViewApi& api){view.LaunchFinished(&api);}
    static void Tick(IGameSproutView& view,GameSproutViewApi& api){view.Update(&api);}
    static void Stop(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
    static bool Start(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);return module.Initialize(&api)&&module.Launch(&api);}
    static void Stop(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
inline void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
inline void Pump(GameSproutRuntime::IGameSproutRuntime& runtime){
    runtime.ProcessPendingEvents();for(int index=0;index<4;++index)QCoreApplication::processEvents();
}
template<class T>T* Find(QWidget* root,const char* name){auto* value=root->findChild<T*>(name);Check(value,name);return value;}
template<class T>class Module : public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Project final:public Module<IProject> {
public:std::string Root;const std::string& GetProjectRoot() const override{return Root;}
};
class Assets final:public Module<IAssetsManager> {
public:
    IAssetMeta* QueryAsset(const Guid&) const override{return nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterAssetListener(IAssetListener* listener) override{std::erase(Listeners,listener);}
    std::vector<IAssetListener*> Listeners;
};
class Library final:public Module<ILibraryManager> {
public:
    std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&) const override{return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid&,uint64_t) override{return nullptr;}
};
class Selection final:public Module<ISelectionManager> {
public:
    SceneObjectSelection Selected;std::vector<ISelectionListener*> Listeners;mutable int Reads=0;
    std::vector<ProjectAssetSelection> GetAssetSelection() const override{++Reads;return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>&) override{}
    SceneObjectSelection GetSceneObjectSelection() const override{++Reads;return Selected;}
    void SetSceneObjectSelection(const SceneObjectSelection& value) override{Selected=value;const auto snapshot=Listeners;for(auto* listener:snapshot)listener->OnSceneObjectSelectionChanged(Selected);}
    bool RegisterSelectionListener(ISelectionListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterSelectionListener(ISelectionListener* listener) override{std::erase(Listeners,listener);}
};
class PlayMode final:public Module<IPlayMode> {
public:
    PlayModeState State;std::vector<IPlayModeListener*> Listeners;
    PlayModeState GetState() const override{return State;}
    bool Play(std::string&) override{return false;}bool Pause(std::string&) override{return false;}
    bool Resume(std::string&) override{return false;}bool Step(std::string&) override{return false;}bool Stop(std::string&) override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterPlayModeListener(IPlayModeListener* listener) override{std::erase(Listeners,listener);}
    void Emit(EditorPlayState value){State.State=value;const auto snapshot=Listeners;for(auto* listener:snapshot)if(std::find(Listeners.begin(),Listeners.end(),listener)!=Listeners.end())listener->OnPlayModeStateChanged(State);}
};
class Host final:public Module<IRuntimeHost> {
public:
    bool Frame=false;
    bool IsFrameActive() const override{return Frame;}
    void SetSimulationPlaying(bool) override{}bool IsSimulationPlaying() const override{return false;}void ResetClock() override{}
    bool RegisterFrameListener(IRuntimeFrameListener*) override{return true;}void UnregisterFrameListener(IRuntimeFrameListener*) override{}
};
class Docker final:public IIDEDocker {
public:
    QWidget* Widget=nullptr;~Docker() override{delete Widget;}
    bool AttachWidget(QWidget* widget) override{if(Widget)return false;Widget=widget;return true;}
    void Layout(eDockerLayout) override{}bool Split(IIDEDocker*,Qt::Orientation) override{return false;}bool Tabify(IIDEDocker*) override{return false;}
    void Activate() override{if(Widget)Widget->show();}void SetTitle(const QString&) override{}
    IIDEMenu* CreateChildMenu(const QString&) override{return nullptr;}
    IContextMenuManager* GetContextMenuManager() const override{return nullptr;}
    bool IsContentVisible() const override{return Widget&&Widget->isVisible();}
    std::map<const void*,fContentVisibilityCallback> VisibilityCallbacks;
    bool RegisterContentVisibilityCallback(const void* owner,const fContentVisibilityCallback& callback) override{return owner&&callback&&VisibilityCallbacks.emplace(owner,callback).second;}
    void UnregisterContentVisibilityCallback(const void* owner) override{VisibilityCallbacks.erase(owner);}
};
class IDE final:public IIDEWindow {
public:
    Docker Inspector;
    IIDEMenuBar* GetMenuBar() const override{return nullptr;}
    IIDEDocker* CreateDocker(const QString& name) override{return name=="Inspector"?&Inspector:nullptr;}
    IIDEDocker* QueryDocker(const QString& name) override{return name=="Inspector"?&Inspector:nullptr;}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation) override{return false;}
    void Loading(const std::string&,const std::string&) override{}void Unloading() override{}void SetLoadingInfo(const std::string&,const std::string&) override{}
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
// Default checks delegate to the real policy. Future capability mode exercises the
// existing .ui actions and subtree interaction without claiming product support.
class AuthoringProbe final:public Module<IStaticAuthoring> {
public:
    IStaticAuthoring* Production=nullptr;ISceneManager* Scenes=nullptr;bool FutureCapabilities=false;
    mutable int Reads=0;int Writes=0;
    GameSproutRuntime::StaticFlags FutureFlags=GameSproutRuntime::StaticFlags::Everything;
    GameSproutRuntime::StaticFlags FutureDefinitions=GameSproutRuntime::StaticFlags::Everything;
    GameSproutRuntime::StaticFlags GetSupportedFlags() const override{++Reads;return FutureCapabilities?FutureFlags:Production->GetSupportedFlags();}
    std::vector<StaticFlagDefinition> GetFlagDefinitions() const override{++Reads;auto values=Production->GetFlagDefinitions();if(FutureCapabilities)for(auto& value:values)value.Supported=(value.Flag&FutureDefinitions)!=GameSproutRuntime::StaticFlags::None;return values;}
    bool SetFlags(std::uint64_t id,GameSproutRuntime::StaticFlags values,GameSproutRuntime::StaticFlags changed,bool children,std::string& error) override{
        ++Writes;if(!FutureCapabilities)return Production->SetFlags(id,values,changed,children,error);
        if(!GameSproutRuntime::IsValidStaticFlags(values)||!GameSproutRuntime::IsValidStaticFlags(changed)){error="Unknown StaticFlags";return false;}
        return Scenes->SetObjectStaticFlags(id,values,changed,children,error);
    }
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;}
};

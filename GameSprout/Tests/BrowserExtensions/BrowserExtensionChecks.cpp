#include "IProjectAssetsUI.h"
#include "IHierarchyUI.h"
#include "IIDEWindow.h"
#include "ISceneManager.h"
#include "IProject.h"
#include "IAssetsManager.h"
#include "GameSproutRuntimeExport.h"
#include "../../View/Shared/AssetDragPayload.h"
#include "../../View/Shared/SceneObjectDragPayload.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QLabel>
#include <QPointer>
#include <QMouseEvent>
#include <QTest>
#include <QTreeView>
#include <map>
#include <iostream>
#include <stdexcept>
namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutView& view,GameSproutViewApi& api){return view.Initialize(&api);}
    static bool Launch(IGameSproutView& view,GameSproutViewApi& api){return view.Launch(&api);}
    static void Finished(IGameSproutView& view,GameSproutViewApi& api){view.LaunchFinished(&api);}
    static void Release(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
};
}
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static void Pump(){for(int i=0;i<4;++i)QCoreApplication::processEvents();}
template<class T>class Module:public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class TestProject final:public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override{return Root;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class TestMeta final:public IAssetMeta {
public:
    TestMeta(const Guid& guid,std::string path):IAssetMeta(eAssetImporterType::Folder,guid),Path(std::move(path)){}
    std::string Path;
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return eAssetImportState::Ready;}
    std::string GetImportError() const override{return {};}
    bool SerializeMeta(std::string& text) const override{text="{}";return true;}
    bool SetSubAssets(const std::string&) override{return true;}
    bool SaveMeta() override{return true;}
};
class TestAssets final:public IAssetsManager {
public:
    TestMeta* Meta=nullptr;std::vector<IAssetListener*> Listeners;
    ~TestAssets() override{delete Meta;}
    IAssetMeta* QueryAsset(const Guid& guid) const override{return Meta&&Meta->_Guid.GetGuidString()==guid.GetGuidString()?Meta:nullptr;}
    bool ReimportAsset(const Guid&) override{return true;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return Meta?std::vector<IAssetMeta*>{Meta}:std::vector<IAssetMeta*>{};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return GetAssetMetas();}
    bool RegisterAssetListener(IAssetListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterAssetListener(IAssetListener* listener) override{Listeners.erase(std::remove(Listeners.begin(),Listeners.end(),listener),Listeners.end());}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Scenes final : public Module<ISceneManager> {
public:
    GameSproutRuntime::IScene* Active=nullptr;
    GameSproutRuntime::IGameObject* Content=nullptr;
    bool Playing=false;
    ISceneListener* Listener=nullptr;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene() const override{return Active;}
    GameSproutRuntime::IGameObject* GetDocumentContentRoot() const override{return Content?Content:Active;}
    bool IsDocumentSessionActive() const override{return Content!=nullptr;}
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

class ContextMenu final:public IContextMenu {
public:
    fMenuClickCallback Callback=nullptr;std::map<std::string,ContextMenu*> Children;
    ~ContextMenu() override{for(const auto& item:Children)delete item.second;}
    IContextMenu* CreateChildMenu(const std::string& name) override{auto*& item=Children[name];if(!item)item=new ContextMenu;return item;}
    void SetClickCalback(fMenuClickCallback callback) override{Callback=callback;}
};
class ContextMenus final:public IContextMenuManager {
public:
    std::map<std::string,ContextMenu*> Menus;
    ~ContextMenus() override{for(const auto& item:Menus)delete item.second;}
    IContextMenu* CreateMenu(const std::string& name) override{auto*& item=Menus[name];if(!item)item=new ContextMenu;return item;}
};
class Menu final:public IIDEMenu {
public:
    IIDEMenu* CreateChildMenu(const QString&) override{return nullptr;}
    void SetClickCallback(fMenuClickCallback) override{}
};
class Docker final:public IIDEDocker {
public:
    QWidget Host;QWidget* Content=nullptr;mutable ContextMenus Context;Menu PanelMenu;
    bool AttachWidget(QWidget* widget) override{Content=widget;widget->setParent(&Host);widget->show();Host.resize(800,600);Host.show();return true;}
    void Layout(eDockerLayout) override{}
    bool Split(IIDEDocker*,Qt::Orientation) override{return true;}
    bool Tabify(IIDEDocker*) override{return true;}
    void Activate() override{}
    void SetTitle(const QString&) override{}
    IIDEMenu* CreateChildMenu(const QString&) override{return &PanelMenu;}
    IContextMenuManager* GetContextMenuManager() const override{return &Context;}
};
class IDE final:public IIDEWindow {
public:
    Docker Project,Hierarchy;
    IIDEMenuBar* GetMenuBar() const override{return nullptr;}
    IIDEDocker* CreateDocker(const QString& name) override{return name=="Hierarchy"?&Hierarchy:&Project;}
    IIDEDocker* QueryDocker(const QString& name) override{return CreateDocker(name);}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation) override{return true;}
    void Loading(const std::string&,const std::string&) override{}
    void Unloading() override{}
    void SetLoadingInfo(const std::string&,const std::string&) override{}
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;}
};
static GameSproutEditor::IGameSproutView* LoadView(const char* path){void* library=dlopen(path,RTLD_NOW|RTLD_LOCAL);const char* error=library?nullptr:dlerror();Check(library,error?error:"Cannot load View");auto entry=reinterpret_cast<GameSproutEditor::IGameSproutView*(*)()>(dlsym(library,GameSproutViewEntryName));Check(entry,"Missing GetView");return entry();}
static QModelIndex Node(QAbstractItemModel* model,uint64_t id,const QModelIndex& parent={}){for(int row=0;row<model->rowCount(parent);++row){auto index=model->index(row,0,parent);if(index.data(Qt::UserRole+1).toULongLong()==id)return index;auto found=Node(model,id,index);if(found.isValid())return found;}return {};}
static bool Drop(QTreeView* tree,const QPoint& position,QMimeData* mime,bool execute=true){QDragEnterEvent enter(position,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&enter);if(!enter.isAccepted()||!execute)return enter.isAccepted();QDragMoveEvent move(position,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&move);QDropEvent event(position,Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(tree->viewport(),&event);return move.isAccepted()&&event.isAccepted();}
static void RightClick(QTreeView* tree,const QPoint& position){QTest::mouseClick(tree->viewport(),Qt::RightButton,Qt::NoModifier,position);}
static int Actions=0;static void Action(){++Actions;}
int main(int argc,char** argv){QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;IHierarchyUI* hierarchy=nullptr;IProjectAssetsUI* projectView=nullptr;
    try {
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create temporary project");QDir root(temporary.path());Check(root.mkpath("Assets/Target"),"Cannot create target directory");
        TestProject project;project.Root=temporary.path().toStdString();TestAssets assets;assets.Meta=new TestMeta(Guid("1234567890abcdef1234567890abcdef"),"Target");
        Scenes scenes;scenes.Active=runtime->CreateScene("Scene");auto* first=scenes.Active->CreateGameObject("First");auto* second=scenes.Active->CreateGameObject("Second");
        IDE ide;Editor editor(runtime);editor.Modules={{"Project",&project},{"AssetsManager",&assets},{"SceneManager",&scenes}};editor.Views={{"IDEWindow",&ide}};
        projectView=dynamic_cast<IProjectAssetsUI*>(LoadView(PROJECT_VIEW_FILE));hierarchy=dynamic_cast<IHierarchyUI*>(LoadView(HIERARCHY_VIEW_FILE));Check(projectView&&hierarchy,"Bad View interfaces");
        struct Scope {IProjectAssetsUI*& Project;IHierarchyUI*& Hierarchy;Editor& Api;~Scope(){if(Hierarchy)GameSproutEditor::GameSproutEditorFinal::Release(*Hierarchy,Api);if(Project)GameSproutEditor::GameSproutEditorFinal::Release(*Project,Api);}} scope{projectView,hierarchy,editor};
        for(auto* view:{static_cast<GameSproutEditor::IGameSproutView*>(projectView),static_cast<GameSproutEditor::IGameSproutView*>(hierarchy)}){GameSproutEditor::GameSproutEditorFinal::Before(*view,editor);Check(GameSproutEditor::GameSproutEditorFinal::Initialize(*view,editor),"View initialize failed");Check(GameSproutEditor::GameSproutEditorFinal::Launch(*view,editor),"View launch failed");GameSproutEditor::GameSproutEditorFinal::Finished(*view,editor);}
        Pump();auto* folderTree=ide.Project.Content->findChild<QTreeView*>("folderTree");auto* assetTree=ide.Project.Content->findChild<QTreeView*>("assetTree");auto* tree=ide.Hierarchy.Content->findChild<QTreeView*>("hierarchyTree");Check(folderTree&&assetTree&&tree&&folderTree->acceptDrops()&&assetTree->acceptDrops()&&tree->acceptDrops(),"Designer drop surfaces missing");tree->expandAll();folderTree->expandAll();Pump();
        // The real Hierarchy .ui owns feature headers; hiding the Hierarchy
        // itself must not lose each feature's requested visible state.
        auto* headerHost=ide.Hierarchy.Content->findChild<QWidget*>("headerHost");
        Check(headerHost&&headerHost->isHidden(),"Empty Hierarchy header occupies space");
        QPointer<QWidget> header=new QLabel("Feature stage");
        Check(hierarchy->AttachHeaderWidget(header)&&!hierarchy->AttachHeaderWidget(header),"Header ownership or duplicate attach failed");
        Pump();Check(headerHost->isVisible()&&header->isVisible(),"Attached feature header is not visible");
        hierarchy->Hide();Check(hierarchy->SetHeaderWidgetVisible(header,false)&&headerHost->isHidden(),"Hidden parent lost feature hidden state");
        Check(hierarchy->SetHeaderWidgetVisible(header,true)&&!headerHost->isHidden(),"Hidden parent prevented header activation");
        hierarchy->Show();Pump();Check(header->isVisible(),"Feature header did not return with Hierarchy");
        Check(hierarchy->SetHeaderWidgetVisible(header,false)&&headerHost->isHidden(),"Inactive feature retained header space");
        Check(hierarchy->RemoveHeaderWidget(header)&&header.isNull(),"Header removal did not destroy feature widget");
        header=new QLabel("Release stage");Check(hierarchy->AttachHeaderWidget(header),"Cannot reattach feature header");
        int projectDrops=0,assetDrops=0;SceneObjectDropContext projectContext;HierarchyAssetDropContext hierarchyContext;int owner=0;
        Check(projectView->RegisterDropHandler(&owner,[](const SceneObjectDropContext& context){return context.SessionID==17;},[&](const SceneObjectDropContext& context,std::string&){++projectDrops;projectContext=context;return true;}),"Register Project drop");
        auto* sceneMime=SceneObjectDragPayload::Encode({17,scenes.Active->GetInstanceID(),second->GetInstanceID()});Check(sceneMime,"Cannot encode scene object");
        const auto folder=folderTree->model()->index(0,0,folderTree->model()->index(0,0));
        Check(Drop(folderTree,folderTree->visualRect(folder).center(),sceneMime)&&projectDrops==1&&projectContext.Directory=="Target"&&projectContext.ObjectID==second->GetInstanceID(),"Folder drop lost directory/object values");
        Check(Drop(assetTree,QPoint(20,assetTree->viewport()->height()-5),sceneMime)&&projectDrops==2&&projectContext.Directory.empty(),"Blank Project drop did not use current directory");
        projectView->UnregisterExtensions(&owner);Check(!Drop(assetTree,QPoint(10,10),sceneMime)&&projectDrops==2,"Unregister retained Project drop callback");delete sceneMime;
        int errorOwner=0;
        Check(projectView->RegisterDropHandler(&errorOwner,[](const SceneObjectDropContext&){return true;},[](const SceneObjectDropContext&,std::string& error){error="Feature refused this drop";return false;}),"Register error feedback feature");
        sceneMime=SceneObjectDragPayload::Encode({17,scenes.Active->GetInstanceID(),second->GetInstanceID()});
        Check(!Drop(assetTree,QPoint(10,10),sceneMime)&&ide.Project.Content->findChild<QLabel*>("statusLabel")->text()=="Feature refused this drop","Drop execution error was not reported in the host status");
        delete sceneMime;projectView->UnregisterExtensions(&errorOwner);
        QMimeData malformed;malformed.setData(SceneObjectDragPayload::MimeType(),QByteArray(31,'x'));
        Check(!Drop(assetTree,QPoint(10,10),&malformed)&&projectDrops==2,"Malformed scene identity reached an extension");
        const ProjectAssetSelection asset{"2345678901abcdef2345678901abcdef",0,false};auto* assetMime=AssetDragPayload::Encode({asset});
        Check(hierarchy->RegisterAssetDropHandler(&owner,[](const HierarchyAssetDropContext& context){return context.Assets.size()==1;},[&](const HierarchyAssetDropContext& context,std::string&){++assetDrops;hierarchyContext=context;return true;}),"Register Hierarchy asset drop");
        const auto firstIndex=Node(tree->model(),first->GetInstanceID()),secondIndex=Node(tree->model(),second->GetInstanceID());tree->setCurrentIndex(firstIndex);
        const auto selected=hierarchy->GetSelectedObjectID();Check(Drop(tree,tree->visualRect(secondIndex).center(),assetMime)&&assetDrops==1&&hierarchyContext.SceneID==scenes.Active->GetInstanceID()&&hierarchyContext.ParentObjectID==second->GetInstanceID()&&hierarchyContext.Assets[0].GuidString==asset.GuidString&&hierarchy->GetSelectedObjectID()==selected,"Asset drop changed Inspector selection or lost parent/asset identity");
        Check(Drop(tree,QPoint(20,tree->viewport()->height()-5),assetMime)&&hierarchyContext.ParentObjectID==0,"Blank Hierarchy drop did not target scene root");
        Check(hierarchy->RegisterContextAction(&owner,{"Feature","Run"},&Action,[](const HierarchyActionContext& context){return context.ObjectID!=0;}),"Register hierarchy context action");
        RightClick(tree,tree->visualRect(secondIndex).center());const auto captured=hierarchy->GetActionContext();tree->setCurrentIndex(firstIndex);auto* menu=ide.Hierarchy.Context.Menus.at("Feature")->Children.at("Run");
        Check(captured.ObjectID==second->GetInstanceID()&&hierarchy->GetActionContext().ObjectID==second->GetInstanceID()&&menu->Callback,"Context action followed global selection instead of right-click target");menu->Callback();Check(Actions==1,"Context action did not invoke");
        RightClick(tree,QPoint(20,tree->viewport()->height()-5));Check(!hierarchy->GetActionContext().ObjectID&&!menu->Callback,"Blank context retained previous object");
        hierarchy->UnregisterExtensions(&owner);Check(!menu->Callback&&!Drop(tree,tree->visualRect(secondIndex).center(),assetMime)&&assetDrops==2,"Unregister retained hierarchy menu/drop callback");delete assetMime;
        // Probes may unregister themselves; dispatch must not execute their old closure.
        int probeOwner=0;Check(projectView->RegisterDropHandler(&probeOwner,[&](const SceneObjectDropContext&){projectView->UnregisterExtensions(&probeOwner);return true;},[&](const SceneObjectDropContext&,std::string&){++projectDrops;return true;}),"Register self-unregister probe");
        sceneMime=SceneObjectDragPayload::Encode({17,scenes.Active->GetInstanceID(),second->GetInstanceID()});Check(!Drop(assetTree,QPoint(10,10),sceneMime)&&projectDrops==2,"Dispatch executed an unregistered feature");delete sceneMime;
        // A temporary document shows only its content tree. Generic creation
        // and blank-area drops target that root, not the synthetic scene.
        scenes.Content=first;auto* inside=scenes.Active->CreateGameObject("Inside content",first);
        scenes.Listener->OnActiveSceneChanged();Pump();
        Check(Node(tree->model(),first->GetInstanceID()).isValid()&&Node(tree->model(),inside->GetInstanceID()).isValid()&&
              !Node(tree->model(),scenes.Active->GetInstanceID()).isValid()&&!Node(tree->model(),second->GetInstanceID()).isValid(),
              "Temporary hierarchy showed the synthetic scene/outside nodes");
        const auto sceneChildren=scenes.Active->GetChildCount(),contentChildren=first->GetChildCount();
        auto* create=ide.Hierarchy.Context.Menus.at("Create")->Children.at("Empty");
        Check(create->Callback,"Document root creation action missing");create->Callback();Pump();
        Check(first->GetChildCount()==contentChildren+1&&scenes.Active->GetChildCount()==sceneChildren,"Default Create escaped the document content root");
        int stageOwner=0;Check(hierarchy->RegisterAssetDropHandler(&stageOwner,[](const HierarchyAssetDropContext&){return true;},
            [&](const HierarchyAssetDropContext& value,std::string&){hierarchyContext=value;return true;}),"Cannot register document drop fixture");
        assetMime=AssetDragPayload::Encode({asset});
        Check(Drop(tree,QPoint(20,tree->viewport()->height()-5),assetMime)&&hierarchyContext.ParentObjectID==first->GetInstanceID(),
              "Blank document drop targeted the synthetic scene");
        hierarchy->UnregisterExtensions(&stageOwner);delete assetMime;
        scenes.Content=nullptr;scenes.Listener->OnActiveSceneChanged();Pump();
        GameSproutEditor::GameSproutEditorFinal::Release(*hierarchy,editor);hierarchy=nullptr;GameSproutEditor::GameSproutEditorFinal::Release(*projectView,editor);projectView=nullptr;
        Check(header.isNull(),"Hierarchy release retained feature header widget");
        Check(!scenes.Listener&&assets.Listeners.empty(),"Release retained Module subscriptions");Pump();delete runtime;runtime=nullptr;
        std::cout<<"PASS generic browser drop routing, captured context targets, selection preservation and extension teardown\n";return 0;
    }catch(const std::exception& error){delete runtime;std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
}

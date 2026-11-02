#pragma once
#include "GameSproutRuntimeExport.h"
#include "AssetTestSupport.h"
#include "IProjectAssetsUI.h"
#include "IInspectorUI.h"
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QTimer>
#include <QDialog>
#include <QApplication>
#include <QTreeView>
#include <QLineEdit>
#include <QComboBox>
#include <QFileInfo>
#include <QItemSelectionModel>

class PreviewDock final : public IIDEDocker {
public:
    QWidget Host;
    QSize Size{900,520};
    QWidget * Content=nullptr;
    bool AttachWidget(QWidget * widget) override {if(!widget||Content)return false;Content=widget;widget->setParent(&Host);Host.resize(Size);widget->setGeometry(Host.rect());widget->show();Host.show();return true;}
    void Layout(eDockerLayout) override {}
    bool Split(IIDEDocker *,Qt::Orientation) override {return true;}
    bool Tabify(IIDEDocker *) override {return true;}
    void Activate() override {Host.show();}
    void SetTitle(const QString & title) override {Host.setWindowTitle(title);}
    IIDEMenu * CreateChildMenu(const QString &) override {return nullptr;}
    IContextMenuManager * GetContextMenuManager() const override {return nullptr;}
    bool IsContentVisible() const override {return Host.isVisible();}
    std::map<const void*,fContentVisibilityCallback> VisibilityCallbacks;
    bool RegisterContentVisibilityCallback(const void* owner,const fContentVisibilityCallback& callback) override {return owner&&callback&&VisibilityCallbacks.emplace(owner,callback).second;}
    void UnregisterContentVisibilityCallback(const void* owner) override {VisibilityCallbacks.erase(owner);}
};
class PreviewIDE final : public IIDEWindow {
public:
    TestIDE Counter;
    PreviewDock Dock;
    PreviewDock InspectorDock;
    PreviewIDE(){InspectorDock.Size=QSize(460,820);}
    void Loading(const std::string & title,const std::string & desc) override {Counter.Loading(title,desc);}
    void Unloading() override {Counter.Unloading();}
    void SetLoadingInfo(const std::string &,const std::string &) override {}
    IIDEMenuBar * GetMenuBar() const override {return nullptr;}
    IIDEDocker * CreateDocker(const QString & name) override {return name=="Inspector"?&InspectorDock:&Dock;}
    IIDEDocker * QueryDocker(const QString & name) override {return name=="Inspector"?&InspectorDock:&Dock;}
    bool ResizeDockers(const QList<IIDEDocker *> &,const QList<int> &,Qt::Orientation) override {return true;}
    bool Initialize(GameSproutEditor::GameSproutViewApi *) override {return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi *) override {return true;}
    void Release(GameSproutEditor::GameSproutViewApi *) override {}
};
class PreviewEditor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit PreviewEditor(GameSproutRuntime::IGameSproutRuntime* runtime=nullptr):GameSproutViewApi(runtime) {}
    ~PreviewEditor() override {delete _Runtime;}
    std::map<std::string,GameSproutEditor::IGameSproutModule *> Modules;
    IIDEWindow * IDE=nullptr;
    std::map<std::string,GameSproutEditor::IGameSproutView *> Views;
    GameSproutEditor::IGameSproutModule * FindGameSproutModule(const std::string & name) const override {const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView * FindGameSproutView(const std::string & name) const override {if(name=="IDEWindow")return IDE;const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;}
};
GameSproutEditor::IGameSproutView * LoadView(const std::string & name) {
#if defined(_WIN32)
    HMODULE handle=LoadLibraryW(std::filesystem::path(std::string(TEST_BINARY_ROOT)+"/Logic/View/"+name+".dll").c_str());
    auto entry=reinterpret_cast<GameSproutEditor::IGameSproutView *(*)()>(GetProcAddress(handle,"GetView"));
#else
#if defined(__APPLE__)
    const char * suffix=".dylib";
#else
    const char * suffix=".so";
#endif
    void * handle=dlopen((std::string(TEST_BINARY_ROOT)+"/Logic/View/lib"+name+suffix).c_str(),RTLD_NOW|RTLD_LOCAL);
    Check(handle!=nullptr,"Cannot load ProjectAssetsUI");
    auto entry=reinterpret_cast<GameSproutEditor::IGameSproutView *(*)()>(dlsym(handle,"GetView"));
#endif
    Check(entry!=nullptr,"ProjectAssetsUI entry missing");return entry();
}
IProjectAssetsUI * LoadProjectView() { return static_cast<IProjectAssetsUI *>(LoadView("ProjectAssetsUI")); }

// Register every View before delivering lifecycle callbacks, as the production
// host does. Feature Views own their Inspector/asset-browser contributions.
std::vector<GameSproutEditor::IGameSproutView*> LoadInspectorFeatureViews(PreviewEditor& editor,bool includeSceneObjects=true) {
    std::vector<GameSproutEditor::IGameSproutView*> result;
    for (const std::string name:{"AnimationUI","AssetImportUI","MaterialUI","SceneObjectUI"}) {
        if(name=="SceneObjectUI"&&!includeSceneObjects)continue;
        auto* view=LoadView(name);editor.Views[name]=view;result.push_back(view);
    }
    return result;
}
void InitializeTestViews(PreviewEditor& editor,const std::vector<GameSproutEditor::IGameSproutView*>& views) {
    for (auto* view:views) GameSproutEditor::GameSproutEditorFinal::Before(view,&editor);
    for (auto* view:views) {
        std::string name="<unregistered>";
        for(const auto& entry:editor.Views)if(entry.second==view){name=entry.first;break;}
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,&editor),"View initialization failed: "+name);
    }
}
void LaunchTestViews(PreviewEditor& editor,const std::vector<GameSproutEditor::IGameSproutView*>& views) {
    for (auto* view:views) Check(GameSproutEditor::GameSproutEditorFinal::Launch(view,&editor),"View launch failed");
    for (auto* view:views) GameSproutEditor::GameSproutEditorFinal::Finished(view,&editor);
    QCoreApplication::processEvents();
}
void ReleaseTestViews(PreviewEditor& editor,std::vector<GameSproutEditor::IGameSproutView*>& views) {
    for (auto iterator=views.rbegin();iterator!=views.rend();++iterator)
        GameSproutEditor::GameSproutEditorFinal::Release(*iterator,&editor);
    views.clear();
}

class Recorder final : public IAssetListener {
public:
    std::vector<AssetEvent> Events;
    const std::thread::id Main=std::this_thread::get_id();
    void Save(const AssetEvent & event){Check(std::this_thread::get_id()==Main,"Asset event on worker thread");Events.push_back(event);}
    void OnAssetCreate(const AssetEvent & event) override {Save(event);}
    void OnAssetModify(const AssetEvent & event) override {Save(event);}
    void OnAssetRemove(const AssetEvent & event) override {Save(event);}
};
QModelIndex FindRow(QTreeView * tree,const QString & name) {
    for(int row=0;row<tree->model()->rowCount();++row){auto index=tree->model()->index(row,0);if(index.data().toString()==name)return index;}
    return {};
}

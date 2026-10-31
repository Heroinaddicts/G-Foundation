#pragma once
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
    PreviewEditor():GameSproutViewApi(nullptr) {}
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

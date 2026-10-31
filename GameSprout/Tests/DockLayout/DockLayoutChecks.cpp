#include "../AssetsImport/ProjectViewTestSupport.h"
#include "IDockerLayoutManager.h"
#include "IConsoleUI.h"
#include <QDockWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QTabBar>
#include <QMenuBar>
#include <QRegion>
#include <QElapsedTimer>
#include <QSplitter>
#include <QStyle>
#include <QTest>
#include <QCursor>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QMenu>
#include <QAction>

class EmptyAssets final : public IAssetsManager {
public:
    IAssetMeta* QueryAsset(const Guid&) const override{return nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&, std::string&, std::string&) const override { return false; }
    bool SaveMaterialSource(const Guid&, const std::string&, const std::string&, std::string&) override { return false; }
    bool ExtractMaterial(const Guid&, uint64_t, const std::string&, std::string&, std::string&) override { return false; }
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener*) override{return true;}
    void UnregisterAssetListener(IAssetListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class DockTestApplication final : public QApplication {
public:
    DockTestApplication(int& argc,char** argv):QApplication(argc,argv){}
    QWidget* Trace=nullptr;
    bool notify(QObject* receiver,QEvent* event) override {
        const bool trace=(receiver==Trace&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease)) || (property("traceDockPress").toBool()&&event->type()==QEvent::MouseButtonPress);
        const bool result=QApplication::notify(receiver,event);
        if(trace) {
            auto* mouse=static_cast<QMouseEvent*>(event);
            std::cout<<"Mouse receiver="<<receiver->metaObject()->className()<<":"<<receiver->objectName().toStdString()<<" type="<<int(event->type())<<" pos="<<mouse->position().x()<<","<<mouse->position().y()<<" buttons="<<int(mouse->buttons())<<" spontaneous="<<event->spontaneous()<<" accepted="<<event->isAccepted()<<" appButtons="<<int(QApplication::mouseButtons())<<std::endl;
        }
        return result;
    }
};
static void Settle(int milliseconds=250) {
    QElapsedTimer clock;clock.start();
    do{QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QCoreApplication::processEvents();QThread::msleep(5);}while(clock.elapsed()<milliseconds);
}
static QTabBar* Tabs(QMainWindow* window,const QString& first,const QString& second) {
    for(auto* bar:window->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly)) {
        bool a=false,b=false;
        for(int index=0;index<bar->count();++index){a|=bar->tabText(index)==first;b|=bar->tabText(index)==second;}
        if(!a||!b||!bar->isVisible()||!window->rect().contains(bar->geometry().center()))continue;
        for(auto* dock:window->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
            if(dock->isFloating()||dock->isHidden()||!window->rect().contains(dock->geometry().center()))continue;
            if(dock->windowTitle()!=first&&dock->windowTitle()!=second)continue;
            if(qAbs(dock->geometry().top()-bar->geometry().bottom()-1)>6||qAbs(dock->geometry().right()-bar->geometry().right())>2)continue;
            QWidget* hit=window->childAt(bar->mapTo(window,bar->tabRect(0).center()));
            if(hit==bar||bar->isAncestorOf(hit))return bar;
        }
    }
    return nullptr;
}
static void Mouse(QWidget* widget,QEvent::Type type,QPoint local,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,QPointF(local),QPointF(widget->mapToGlobal(local)),button,buttons,Qt::NoModifier);QCoreApplication::sendEvent(widget,&event);
}
static void Drag(QWidget* widget,QPoint begin,QPoint end) {
    QWindow* window=widget->window()->windowHandle();Check(window,"Mouse drag host has no platform window");
    const QPoint savedCursor=QCursor::pos(),globalBegin=widget->mapToGlobal(begin),globalEnd=widget->mapToGlobal(end);
    const auto position=[&](QPoint global){return window->mapFromGlobal(global);};
    QCursor::setPos(globalBegin);QTest::mouseMove(window,position(globalBegin),10);QCoreApplication::processEvents();
    QWidget* under=widget->childAt(begin);
    std::cout<<"Drag hover cursor="<<int(widget->cursor().shape())<<" child="<<(under?under->objectName().toStdString():"<host>")<<std::endl;
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,position(globalBegin));
    for(int step=1;step<=12;++step){const QPoint point=globalBegin+(globalEnd-globalBegin)*step/12;QCursor::setPos(point);QTest::mouseMove(window,position(point),5);QCoreApplication::processEvents();}
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,position(globalEnd));Settle();QCursor::setPos(savedCursor);
}
static std::string Size(QSize size){return std::to_string(size.width())+"x"+std::to_string(size.height());}
static std::string Rect(QRect rect){return std::to_string(rect.x())+","+std::to_string(rect.y())+" "+Size(rect.size());}
static void Describe(const char* label,QWidget* widget) {
    std::cout<<label<<" geom="<<Rect(widget->geometry())<<" min="<<Size(widget->minimumSize())<<" minHint="<<Size(widget->minimumSizeHint())<<" hint="<<Size(widget->sizeHint())<<" visible="<<widget->isVisible()<<std::endl;
}
static std::vector<QRect> BlankRegions(QMainWindow* host) {
    QRegion blank(host->contentsRect());
    if(host->menuBar()->isVisible())blank-=host->menuBar()->geometry();
    for(auto* dock:host->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))if(!dock->isFloating()&&!dock->isHidden())blank-=dock->geometry();
    for(auto* bar:host->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly))if(bar->isVisible()&&host->rect().contains(bar->geometry().center()))blank-=bar->geometry();
    std::vector<QRect> result;
    for(const QRect& rect:blank)if(rect.width()>=40&&rect.height()>=40)result.push_back(rect);
    return result;
}
#include "DockDragChecks.h"
#include "DockRestoreChecks.h"
int main(int argc,char** argv) {
    DockTestApplication app(argc,argv);const bool explore=QCoreApplication::arguments().contains("--explore");
    IIDEWindow* ide=nullptr;IProjectAssetsUI* project=nullptr;IConsoleUI* console=nullptr;IDockerLayoutManager* layouts=nullptr;
    PreviewEditor editor;EmptyAssets assets;
    const auto cleanup=[&] {
        if(layouts){GameSproutEditor::GameSproutEditorFinal::Release(layouts,&editor);layouts=nullptr;}
        if(project){GameSproutEditor::GameSproutEditorFinal::Release(project,&editor);project=nullptr;}
        if(console){GameSproutEditor::GameSproutEditorFinal::Release(console,&editor);console=nullptr;}
        if(ide){GameSproutEditor::GameSproutEditorFinal::Release(ide,&editor);ide=nullptr;}
    };
    try {
        ide=static_cast<IIDEWindow*>(LoadView("IDEWindow"));editor.IDE=ide;editor.Modules["AssetsManager"]=&assets;
        project=LoadProjectView();console=static_cast<IConsoleUI*>(LoadView("ConsoleUI"));layouts=static_cast<IDockerLayoutManager*>(LoadView("DockerLayoutManager"));
        for(GameSproutEditor::IGameSproutView* view:{static_cast<GameSproutEditor::IGameSproutView*>(console),static_cast<GameSproutEditor::IGameSproutView*>(layouts),static_cast<GameSproutEditor::IGameSproutView*>(project)})GameSproutEditor::GameSproutEditorFinal::Before(view,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(ide,&editor),"IDE init failed");
        for(GameSproutEditor::IGameSproutView* view:{static_cast<GameSproutEditor::IGameSproutView*>(console),static_cast<GameSproutEditor::IGameSproutView*>(layouts),static_cast<GameSproutEditor::IGameSproutView*>(project)})Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,&editor),"Dock view init failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(ide,&editor),"IDE launch failed");
        for(GameSproutEditor::IGameSproutView* view:{static_cast<GameSproutEditor::IGameSproutView*>(console),static_cast<GameSproutEditor::IGameSproutView*>(layouts),static_cast<GameSproutEditor::IGameSproutView*>(project)})Check(GameSproutEditor::GameSproutEditorFinal::Launch(view,&editor),"Dock view launch failed");
        for(const QString& name:{"Hierarchy","Scene","Game","Inspector"}) {
            auto* dock=ide->CreateDocker(name);auto* content=new QWidget();content->setMinimumSize(40,40);content->setObjectName(name+"TestContent");Check(dock&&dock->AttachWidget(content),"Placeholder dock attach failed");
        }
        ide->Show();Settle();
        QMainWindow* host=nullptr;
        for(QWidget* top:QApplication::topLevelWidgets())if(auto* candidate=qobject_cast<QMainWindow*>(top))if(candidate->findChild<QDockWidget*>("ProjectAssets")){host=candidate;break;}
        Check(host,"IDE host missing");host->resize(1280,800);Settle();
        if(explore)app.Trace=host;
        Check(layouts->ResetDefaultLayout(),"Default layout failed");Settle(400);
        auto* projectDock=host->findChild<QDockWidget*>("ProjectAssets");auto* consoleDock=host->findChild<QDockWidget*>("Console");auto* sceneDock=host->findChild<QDockWidget*>("Scene");
        Check(projectDock&&consoleDock&&sceneDock&&projectDock->widget()&&consoleDock->widget(),"Real dock content missing");
        Describe("Host",host);Describe("Project",projectDock);Describe("Project.ui",projectDock->widget());Describe("Console",consoleDock);Describe("Console.ui",consoleDock->widget());Describe("Scene",sceneDock);
        auto* projectStatus=projectDock->findChild<QLabel*>("statusLabel");
        if(projectStatus)Describe("Project status label",projectStatus);
        if(auto* splitter=projectDock->findChild<QSplitter*>("splitter"))Describe("Project asset splitter",splitter);
        if(host->centralWidget())Describe("Central",host->centralWidget());
        if(auto* splitter=consoleDock->findChild<QSplitter*>("consoleSplitter")) {
            Describe("Console splitter",splitter);for(int index=0;index<splitter->count();++index)Describe(index==0?"Console logList":"Console details",splitter->widget(index));
        }
        auto* bottom=Tabs(host,"Project","Console");Check(bottom,"Project/Console bottom tabs missing");Describe("Bottom tabs",bottom);
        Check(host->grab().save("/tmp/gamesprout-dock-before.png"),"Cannot capture default layout");
        if(!explore) {
            auto* hierarchyDock=DockWidget(host,"Hierarchy");auto* inspectorDock=DockWidget(host,"Inspector");
            Describe("Hierarchy",hierarchyDock);Describe("Inspector",inspectorDock);
            Check(qAbs(projectDock->geometry().left()-hierarchyDock->geometry().left())<=2&&qAbs(projectDock->geometry().right()-sceneDock->geometry().right())<=2,"Default Project group must span both Hierarchy and Scene columns");
            Check(qAbs(hierarchyDock->geometry().bottom()-sceneDock->geometry().bottom())<=2,"Hierarchy extends below the shared Project separator");
            Check(inspectorDock->geometry().top()<=hierarchyDock->geometry().top()+2&&inspectorDock->geometry().bottom()>=projectDock->geometry().bottom()-2,"Default Inspector must remain full-height on the right");
        }
        const auto blanks=BlankRegions(host);for(const auto& rect:blanks)std::cout<<"Large uncovered client region="<<Rect(rect)<<std::endl;
        const int before=projectDock->height()+bottom->height();
        const QPoint begin(sceneDock->geometry().center().x(),(sceneDock->geometry().bottom()+bottom->geometry().top())/2);
        const QPoint end(begin.x(),host->contentsRect().bottom()-75);
        std::cout<<"Splitter drag begin="<<begin.x()<<","<<begin.y()<<" end="<<end.x()<<","<<end.y()<<" separatorExtent="<<host->style()->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent)<<std::endl;
        Drag(host,begin,end);
        bottom=Tabs(host,"Project","Console");Check(bottom,"Bottom tab group disappeared after splitter drag");
        const int after=projectDock->height()+bottom->height();
        std::cout<<"Bottom group height before="<<before<<" after drag-to-75px="<<after<<std::endl;
        Describe("Project after drag",projectDock);Describe("Console after drag",consoleDock);Describe("Scene after drag",sceneDock);
        Check(host->grab().save("/tmp/gamesprout-dock-after.png"),"Cannot capture resized layout");
        if(explore) {
            ide->ResizeDockers({ide->QueryDocker("Scene"),ide->QueryDocker("ProjectAssets")},{900,100},Qt::Vertical);Settle();
            bottom=Tabs(host,"Project","Console");
            std::cout<<"Bottom group after explicit ResizeDockers(90/10)="<<projectDock->height()+bottom->height()<<std::endl;
        }
        if(!explore){Check(after<before-40,"Actual splitter could not reduce bottom group");Check(after<200,"Inactive Project/Console size hints prevent compact bottom area");Check(projectStatus&&projectStatus->height()<=40,"Project status text consumes the asset browser area");Check(blanks.empty(),"Default dock layout reserved a large unused central region");}
        QWidget* const projectContent=projectDock->widget();QWidget* const consoleContent=consoleDock->widget();
        console->ConsoleLog("DockCheck.cpp",1,eConsoleLog::Debug,"Dock state survives floating and layout restore");Settle(30);
        auto* logs=consoleDock->findChild<QTreeWidget*>("logList");Check(logs&&logs->topLevelItemCount()==1,"Console log fixture missing");logs->setCurrentItem(logs->topLevelItem(0));
        auto* details=consoleDock->findChild<QPlainTextEdit*>("detailText");Check(details&&details->toPlainText().contains("survives floating"),"Console detail fixture missing");
        auto* menu=consoleDock->findChild<QMenu*>("DockerContextMenu.Console");Check(menu,"Console docker menu missing");
        auto* floatAction=menu->findChild<QAction*>("DockerFloat");auto* closeAction=menu->findChild<QAction*>("DockerClose");Check(floatAction&&closeAction,"Floating/close commands missing");
        floatAction->trigger();Settle();Check(consoleDock->isFloating(),"Actual Float menu did not detach Console");
        closeAction->trigger();Settle();Check(consoleDock->isHidden(),"Actual Close menu did not hide floating Console");
        Check(layouts->ResetDefaultLayout(),"Default reset failed after float/close");Settle();
        Check(!consoleDock->isFloating()&&!consoleDock->isHidden()&&host->tabifiedDockWidgets(projectDock).contains(consoleDock),"Default reset did not restore Project/Console group");
        Check(projectDock->widget()==projectContent&&consoleDock->widget()==consoleContent&&logs->topLevelItemCount()==1&&details->toPlainText().contains("survives floating"),"Dock operations recreated content or lost Console data");
        if(!explore){if(!QCoreApplication::arguments().contains("--restore-only"))CheckDockDragInteractions(ide,layouts,host);CheckDockRestoreInteractions(ide,layouts,host);}
        cleanup();std::cout<<"PASS actual Project/Console forms, default dock geometry and real separator drag"<<(explore?" (evidence only)":"")<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';cleanup();return 1;}
}

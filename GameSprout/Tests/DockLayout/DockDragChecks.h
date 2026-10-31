#pragma once
#include <QKeyEvent>
#include <QWindow>

struct DockDragSource {QWidget* Widget=nullptr;QPoint Point;};
static QDockWidget* DockWidget(QMainWindow* host,const QString& name) {
    auto* dock=host->findChild<QDockWidget*>(name);Check(dock,"Missing dock: "+name.toStdString());return dock;
}
static int TabIndex(QTabBar* bar,const QString& text) {
    for(int index=0;index<bar->count();++index)if(bar->tabText(index)==text)return index;
    throw std::runtime_error("Missing tab: "+text.toStdString());
}
static QRect VisibleGroup(QMainWindow* host,QDockWidget* dock);
static DockDragSource DragSource(QMainWindow* host,QDockWidget* dock) {
    const QRect group=dock->isFloating()?QRect():VisibleGroup(host,dock);
    if(!dock->isFloating())for(auto* bar:host->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(!bar->isVisible()||!host->rect().contains(bar->geometry().center()))continue;
        if(qAbs(group.top()-bar->geometry().bottom()-1)>6||qAbs(group.right()-bar->geometry().right())>2)continue;
        for(int index=0;index<bar->count();++index)if(bar->tabText(index)==dock->windowTitle()) {
            QWidget* hit=host->childAt(bar->mapTo(host,bar->tabRect(index).center()));
            if(hit==bar||bar->isAncestorOf(hit))return {bar,bar->tabRect(index).center()};
        }
    }
    QWidget* title=dock->titleBarWidget();Check(title&&title->isVisible()&&title->height()>0,"Dock drag title is unavailable");
    return {title,QPoint(qMin(title->width()-8,qMax(60,title->width()/2)),title->height()/2)};
}
static QRect VisibleGroup(QMainWindow* host,QDockWidget* dock) {
    QList<QDockWidget*> peers=host->tabifiedDockWidgets(dock);peers.prepend(dock);
    for(auto* peer:peers)if(!peer->isFloating()&&!peer->isHidden()&&host->rect().contains(peer->geometry().center()))return peer->geometry();
    throw std::runtime_error("Dock group has no visible representative");
}
static QRect DockWorkspace(QMainWindow* host) {
    QRect result;
    for(auto* dock:host->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))if(!dock->isFloating()&&!dock->isHidden()&&host->rect().contains(dock->geometry().center()))result=result.isNull()?dock->geometry():result.united(dock->geometry());
    for(auto* bar:host->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly))if(bar->isVisible()&&host->rect().contains(bar->geometry().center()))result=result.united(bar->geometry());
    return result;
}
static void DragDock(QMainWindow* host,QObject* controller,QWidget* source,QPoint sourcePoint,QPoint globalEnd,
                     const std::function<void()>& preview,bool cancel=false) {
    const QPoint globalBegin=source->mapToGlobal(sourcePoint);
    QWindow* pressWindow=source->window()->windowHandle();QWindow* moveWindow=host->windowHandle();
    Check(pressWindow&&moveWindow,"Dock gesture has no native Qt window");
    auto* hit=QApplication::widgetAt(globalBegin);
    std::cout<<"Dock press source="<<source->metaObject()->className()<<":"<<source->objectName().toStdString()<<" local="<<sourcePoint.x()<<","<<sourcePoint.y()<<" global="<<globalBegin.x()<<","<<globalBegin.y()<<" hit="<<(hit?hit->metaObject()->className():"<null>")<<":"<<(hit?hit->objectName().toStdString():"")<<std::endl;
    QTest::mouseMove(pressWindow,pressWindow->mapFromGlobal(globalBegin),5);
    qApp->setProperty("traceDockPress",true);
    QTest::mousePress(pressWindow,Qt::LeftButton,Qt::NoModifier,pressWindow->mapFromGlobal(globalBegin));
    qApp->setProperty("traceDockPress",false);
    try {
        Check(controller->property("armed").toBool(),"Docker controller did not arm actual title/tab press");
        QTest::mouseMove(moveWindow,moveWindow->mapFromGlobal(globalBegin+QPoint(QApplication::startDragDistance()+20,2)),5);QCoreApplication::processEvents();
        Check(controller->property("dragging").toBool(),"Dock drag did not cross threshold");
        QTest::mouseMove(moveWindow,moveWindow->mapFromGlobal(globalEnd),10);QCoreApplication::processEvents();
        preview();
        if(cancel)QTest::keyClick(moveWindow,Qt::Key_Escape);
        QTest::mouseRelease(moveWindow,Qt::LeftButton,Qt::NoModifier,moveWindow->mapFromGlobal(globalEnd));Settle();
    }catch(...) {
        QTest::keyClick(moveWindow,Qt::Key_Escape);
        QTest::mouseRelease(moveWindow,Qt::LeftButton,Qt::NoModifier,moveWindow->mapFromGlobal(globalEnd));throw;
    }
    Check(!controller->property("armed").toBool()&&!controller->property("dragging").toBool(),"Dock transaction retained capture after release/cancel");
    auto* overlay=host->findChild<QWidget*>("DockerDropOverlay");Check(overlay&&!overlay->isVisible(),"Dock preview remained after transaction");
}
static void CheckDockDragInteractions(IIDEWindow* ide,IDockerLayoutManager* layouts,QMainWindow* host) {
    QObject* controller=host->findChild<QObject*>("DockerDragController");Check(controller,"New docker drag controller missing");
    auto* overlay=host->findChild<QWidget*>("DockerDropOverlay");Check(overlay,"Dock preview form missing");
    auto* project=DockWidget(host,"ProjectAssets");auto* console=DockWidget(host,"Console");auto* scene=DockWidget(host,"Scene");auto* game=DockWidget(host,"Game");
    QWidget* const projectContent=project->widget();
    const auto reset=[&]{
        Check(layouts->ResetDefaultLayout(),"Default reset failed during dock gesture checks");Settle();ide->QueryDocker("ProjectAssets")->Activate();Settle(30);
        const QRect bottom=VisibleGroup(host,project),left=VisibleGroup(host,DockWidget(host,"Hierarchy")),middle=VisibleGroup(host,scene),right=VisibleGroup(host,DockWidget(host,"Inspector"));
        Check(qAbs(bottom.left()-left.left())<=2&&qAbs(bottom.right()-middle.right())<=2&&qAbs(left.bottom()-middle.bottom())<=2,"Repeated default reset lost the Project span below Hierarchy and Scene");
        Check(right.top()<=left.top()+2&&right.bottom()>=bottom.bottom()-2,"Repeated default reset lost full-height Inspector");
    };
    const auto checkPreview=[&](int position,bool root) {
        Check(controller->property("hasDropTarget").toBool()&&controller->property("dropPosition").toInt()==position&&controller->property("rootDrop").toBool()==root,"Dock preview expected position="+std::to_string(position)+" root="+std::to_string(root)+" but got target="+std::to_string(controller->property("hasDropTarget").toBool())+" position="+std::to_string(controller->property("dropPosition").toInt())+" root="+std::to_string(controller->property("rootDrop").toBool()));
        Check(overlay->isVisible()&&controller->property("previewGeometry").toRect().isValid(),"Dock target did not show preview rectangle");
    };
    // Preview is a transaction: no reparenting or geometry changes until release; Escape is lossless.
    reset();auto source=DragSource(host,project);const QByteArray beforeCancel=host->saveState(1);
    const QPoint sceneCenter=host->mapToGlobal(VisibleGroup(host,scene).center());
    DragDock(host,controller,source.Widget,source.Point,sceneCenter,[&]{checkPreview(0,false);Check(host->saveState(1)==beforeCancel,"Preview changed dock layout before release");},true);
    Check(host->saveState(1)==beforeCancel,"Escape changed dock layout");

    // Selected tab moves relative to an existing tab group; the Scene/Game group remains intact.
    for(int position=1;position<=4;++position) {
        reset();const QRect target=VisibleGroup(host,scene);QPoint point=target.center();
        if(position==1)point.setX(target.left()+target.width()/6);
        if(position==2)point.setX(target.right()-target.width()/6);
        if(position==3)point.setY(target.top()+target.height()/6);
        if(position==4)point.setY(target.bottom()-target.height()/6);
        source=DragSource(host,project);
        std::cout<<"Dock local drop zone "<<position<<std::endl;
        DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(point),[&]{checkPreview(position,false);});
        std::cout<<"Local result Project="<<Rect(project->geometry())<<" Scene="<<Rect(scene->geometry())<<" Game="<<Rect(game->geometry())<<" Scene peers=";
        for(auto* peer:host->tabifiedDockWidgets(scene))std::cout<<peer->objectName().toStdString()<<",";
        std::cout<<std::endl;
        Check(!project->isFloating()&&host->tabifiedDockWidgets(scene).contains(game)&&!host->tabifiedDockWidgets(scene).contains(project),"Local split broke the target tab group");
        const QRect moved=VisibleGroup(host,project),anchor=VisibleGroup(host,scene);
        if(position==1)Check(moved.right()<anchor.left(),"Left drop did not place pane left of group");
        if(position==2)Check(moved.left()>anchor.right(),"Right drop did not place pane right of group");
        if(position==3)Check(moved.bottom()<anchor.top(),"Top drop did not place pane above group");
        if(position==4)Check(moved.top()>anchor.bottom(),"Bottom drop did not place pane below group");
    }
    // Root edges span the workspace, rather than splitting whichever pane lies under the cursor.
    for(int position=1;position<=4;++position) {
        reset();const QRect workspace=DockWorkspace(host);QPoint point=workspace.center();
        if(position==1)point.setX(workspace.left()+2);
        if(position==2)point.setX(workspace.right()-2);
        if(position==3)point.setY(workspace.top()+2);
        if(position==4)point.setY(workspace.bottom()-2);
        source=DragSource(host,project);std::cout<<"Dock root drop zone "<<position<<std::endl;
        DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(point),[&]{checkPreview(position,true);});
        const QRect moved=VisibleGroup(host,project);QRect other;
        for(auto* dock:host->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))if(dock!=project&&!dock->isFloating()&&!dock->isHidden()&&host->rect().contains(dock->geometry().center()))other=other.isNull()?dock->geometry():other.united(dock->geometry());
        if(position==1)Check(moved.right()<other.left(),"Root left drop did not span the outside of remaining groups");
        if(position==2)Check(moved.left()>other.right(),"Root right drop did not span the outside of remaining groups");
        if(position==3)Check(moved.bottom()<other.top(),"Root top drop did not span the outside of remaining groups");
        if(position==4)Check(moved.top()>other.bottom(),"Root bottom drop did not span the outside of remaining groups");
        Check(BlankRegions(host).empty(),"Root split left an uncovered client hole");
    }
    // Reorder within the tab bar uses its insertion gap, without creating a floating window.
    reset();auto* bar=Tabs(host,"Project","Console");Check(bar,"Reorder source tabs missing");
    const int projectIndex=TabIndex(bar,"Project"),consoleIndex=TabIndex(bar,"Console");
    const QPoint gap=bar->mapToGlobal(QPoint(bar->tabRect(consoleIndex).right()-2,bar->tabRect(consoleIndex).center().y()));
    DragDock(host,controller,bar,bar->tabRect(projectIndex).center(),gap,[&]{checkPreview(0,false);Check(controller->property("tabInsertionIndex").toInt()==consoleIndex+1&&controller->property("previewGeometry").toRect().width()<=20,"Tab reorder did not preview insertion gap");});
    bar=Tabs(host,"Project","Console");Check(bar&&TabIndex(bar,"Console")<TabIndex(bar,"Project")&&!project->isFloating(),"Tab insertion order was not committed");

    // Drag a tab outside to float, then use its real title to dock into Scene/Game tabs.
    reset();source=DragSource(host,project);const QPoint outside=host->mapToGlobal(QPoint(host->width()+150,host->height()/2));
    DragDock(host,controller,source.Widget,source.Point,outside,[&]{Check(!controller->property("hasDropTarget").toBool()&&!overlay->isVisible(),"Outside float showed an unrelated drop target");});
    Check(project->isFloating()&&!console->isFloating()&&project->widget()==projectContent,"Dragging tab outside did not detach only that tab");
    source=DragSource(host,project);
    DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(VisibleGroup(host,scene).center()),[&]{checkPreview(0,false);});
    Check(!project->isFloating()&&host->tabifiedDockWidgets(scene).contains(project)&&host->tabifiedDockWidgets(scene).contains(game),"Floating title did not merge with target tab group");
    Check(project->widget()==projectContent,"Dock drag recreated Project contents");

    // Removing a one-pane group must collapse its empty rectangle.
    reset();auto* inspector=DockWidget(host,"Inspector");ide->QueryDocker("Inspector")->Activate();Settle(30);source=DragSource(host,inspector);
    DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(VisibleGroup(host,scene).center()),[&]{checkPreview(0,false);});
    Check(host->tabifiedDockWidgets(scene).contains(inspector)&&BlankRegions(host).empty(),"Empty source group was not collapsed after tab merge");
    reset();Check(host->grab().save("/tmp/gamesprout-dock-final.png"),"Cannot capture final restored dock layout");
}

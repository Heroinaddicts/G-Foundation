#pragma once
#include <QKeyEvent>
#include <QPointer>
#include <QScreen>
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
static QPoint DropOnTabBar(QMainWindow* host,QDockWidget* dock) {
    const auto target=DragSource(host,dock);
    return target.Widget->mapToGlobal(target.Point);
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
static QRect GlobalDockRect(QDockWidget* dock) {
    return QRect(dock->mapToGlobal(QPoint()),dock->size());
}
static void CheckDropRectangle(const QRect& preview,QDockWidget* dock) {
    const auto finalRect=GlobalDockRect(dock);
    constexpr int roundingTolerance=6;
    Check(qAbs(preview.left()-finalRect.left())<=roundingTolerance &&
        qAbs(preview.top()-finalRect.top())<=roundingTolerance &&
        qAbs(preview.right()-finalRect.right())<=roundingTolerance &&
        qAbs(preview.bottom()-finalRect.bottom())<=roundingTolerance,
        "Drop preview "+Rect(preview)+" differs from final dock "+Rect(finalRect));
}
static void DragDock(QMainWindow* host,QObject* controller,QWidget* source,QPoint sourcePoint,QPoint globalEnd,
                     const std::function<void()>& preview,bool cancel=false) {
    const QPoint globalBegin=source->mapToGlobal(sourcePoint);
    QWindow* pressWindow=source->window()->windowHandle();QWindow* moveWindow=host->windowHandle();
    Check(pressWindow&&moveWindow,"Dock gesture has no native Qt window");
    QTest::mouseMove(pressWindow,pressWindow->mapFromGlobal(globalBegin),5);
    QTest::mousePress(pressWindow,Qt::LeftButton,Qt::NoModifier,pressWindow->mapFromGlobal(globalBegin));
    const auto move=[&](QPoint global) {
        Mouse(host,QEvent::MouseMove,host->mapFromGlobal(global),Qt::NoButton,Qt::LeftButton);
        QCoreApplication::processEvents();
    };
    const auto release=[&] {
        Mouse(host,QEvent::MouseButtonRelease,host->mapFromGlobal(globalEnd),Qt::LeftButton,Qt::NoButton);
        QTest::mouseRelease(moveWindow,Qt::LeftButton,Qt::NoModifier,QPoint(1,1));
    };
    try {
        Check(controller->property("armed").toBool(),"Docker controller did not arm actual title/tab press");
        move(globalBegin+QPoint(QApplication::startDragDistance()+20,2));
        Check(controller->property("dragging").toBool(),"Dock drag did not cross threshold");
        move(globalEnd);
        preview();
        if(cancel)QTest::keyClick(moveWindow,Qt::Key_Escape);
        release();Settle();
    }catch(...) {
        QTest::keyClick(moveWindow,Qt::Key_Escape);
        release();throw;
    }
    Check(!controller->property("armed").toBool()&&!controller->property("dragging").toBool(),"Dock transaction retained capture after release/cancel");
    auto* overlay=host->findChild<QWidget*>("DockerDropOverlay");Check(overlay&&!overlay->isVisible(),"Dock preview remained after transaction");
}
static void CheckDockDragInteractions(IIDEWindow* ide,IDockerLayoutManager* layouts,QMainWindow* host) {
    QObject* controller=host->findChild<QObject*>("DockerDragController");Check(controller,"New docker drag controller missing");
    auto* overlay=host->findChild<QWidget*>("DockerDropOverlay");Check(overlay,"Dock preview form missing");
    auto* project=DockWidget(host,"ProjectAssets");auto* console=DockWidget(host,"Console");auto* scene=DockWidget(host,"Scene");auto* game=DockWidget(host,"Game");
    auto* hierarchy=DockWidget(host,"Hierarchy");auto* inspector=DockWidget(host,"Inspector");
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
    DragDock(host,controller,source.Widget,source.Point,DropOnTabBar(host,scene),[&]{checkPreview(0,false);Check(host->saveState(1)==beforeCancel,"Preview changed dock layout before release");},true);
    Check(host->saveState(1)==beforeCancel,"Escape changed dock layout");
    DragDock(host,controller,source.Widget,source.Point,sceneCenter,[&]{
        Check(!controller->property("hasDropTarget").toBool(),"Dock content center offered a tab merge");
    });
    Check(host->tabifiedDockWidgets(console).contains(project)&&!host->tabifiedDockWidgets(scene).contains(project),
        "Dropping into the content center did not preserve the original tab group");

    // Selected tab moves relative to an existing tab group; the Scene/Game group remains intact.
    for(int position=1;position<=4;++position) {
        reset();const QRect target=VisibleGroup(host,scene);QPoint point=target.center();
        if(position==1)point.setX(target.left()+target.width()/6);
        if(position==2)point.setX(target.right()-target.width()/6);
        if(position==3)point.setY(target.top()+target.height()/6);
        if(position==4)point.setY(target.bottom()-target.height()/6);
        source=DragSource(host,project);QRect preview;
        std::cout<<"Dock local drop zone "<<position<<std::endl;
        DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(point),[&]{checkPreview(position,false);preview=controller->property("previewGeometry").toRect();});
        std::cout<<"Local result Project="<<Rect(project->geometry())<<" Scene="<<Rect(scene->geometry())<<" Game="<<Rect(game->geometry())<<" Scene peers=";
        for(auto* peer:host->tabifiedDockWidgets(scene))std::cout<<peer->objectName().toStdString()<<",";
        std::cout<<std::endl;
        Check(!project->isFloating()&&host->tabifiedDockWidgets(scene).contains(game)&&!host->tabifiedDockWidgets(scene).contains(project),"Local split broke the target tab group");
        const QRect moved=VisibleGroup(host,project),anchor=VisibleGroup(host,scene);
        if(position==1)Check(moved.right()<anchor.left(),"Left drop did not place pane left of group");
        if(position==2)Check(moved.left()>anchor.right(),"Right drop did not place pane right of group");
        if(position==3)Check(moved.bottom()<anchor.top(),"Top drop did not place pane above group");
        if(position==4)Check(moved.top()>anchor.bottom(),"Bottom drop did not place pane below group");
        CheckDropRectangle(preview,project);
    }
    // Root edges span the workspace, rather than splitting whichever pane lies under the cursor.
    for(int position=1;position<=4;++position) {
        reset();const QRect workspace=DockWorkspace(host);QPoint point=workspace.center();
        if(position==1)point.setX(workspace.left()+2);
        if(position==2)point.setX(workspace.right()-2);
        if(position==3)point.setY(workspace.top()-16);
        if(position==4)point.setY(workspace.bottom()-2);
        source=DragSource(host,project);QRect preview;std::cout<<"Dock root drop zone "<<position<<std::endl;
        DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(point),[&]{checkPreview(position,true);preview=controller->property("previewGeometry").toRect();});
        const QRect moved=VisibleGroup(host,project);QRect other;
        for(auto* dock:host->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))if(dock!=project&&!dock->isFloating()&&!dock->isHidden()&&host->rect().contains(dock->geometry().center()))other=other.isNull()?dock->geometry():other.united(dock->geometry());
        if(position==1)Check(moved.right()<other.left(),"Root left drop did not span the outside of remaining groups");
        if(position==2)Check(moved.left()>other.right(),"Root right drop did not span the outside of remaining groups");
        if(position==3)Check(moved.bottom()<other.top(),"Root top drop did not span the outside of remaining groups");
        if(position==4)Check(moved.top()>other.bottom(),"Root bottom drop did not span the outside of remaining groups");
        Check(BlankRegions(host).empty(),"Root split left an uncovered client hole");
        CheckDropRectangle(preview,project);
    }
    // Reorder within the tab bar uses its insertion gap, without creating a floating window.
    reset();auto* bar=Tabs(host,"Project","Console");Check(bar,"Reorder source tabs missing");
    const int projectIndex=TabIndex(bar,"Project"),consoleIndex=TabIndex(bar,"Console");
    const QPoint gap=bar->mapToGlobal(QPoint(bar->tabRect(consoleIndex).right()-2,bar->tabRect(consoleIndex).center().y()));
    const QRect beforeReorderScene=VisibleGroup(host,scene),beforeReorderLeft=hierarchy->geometry(),beforeReorderRight=inspector->geometry();
    int unrelatedVisibilityChanges=0;
    const auto sceneVisibility=QObject::connect(scene,&QDockWidget::visibilityChanged,host,[&]{++unrelatedVisibilityChanges;});
    const auto inspectorVisibility=QObject::connect(inspector,&QDockWidget::visibilityChanged,host,[&]{++unrelatedVisibilityChanges;});
    DragDock(host,controller,bar,bar->tabRect(projectIndex).center(),gap,[&]{checkPreview(0,false);Check(controller->property("tabInsertionIndex").toInt()==consoleIndex+1&&controller->property("previewGeometry").toRect().width()<=20,"Tab reorder did not preview insertion gap");});
    QObject::disconnect(sceneVisibility);QObject::disconnect(inspectorVisibility);
    bar=Tabs(host,"Project","Console");Check(bar&&TabIndex(bar,"Console")<TabIndex(bar,"Project")&&!project->isFloating(),"Tab insertion order was not committed");
    Check(unrelatedVisibilityChanges==0&&VisibleGroup(host,scene)==beforeReorderScene&&hierarchy->geometry()==beforeReorderLeft&&inspector->geometry()==beforeReorderRight,
        "Tab reorder modified an unrelated dock group");

    // A layout edit may change Qt's internal dock area while leaving three visible columns.
    // Window growth must still go into Scene/Game and preserve user-selected sidebar widths.
    const int previousWidth=host->width();
    const QPoint splitter((VisibleGroup(host,scene).right()+inspector->geometry().left())/2,VisibleGroup(host,scene).center().y());
    Drag(host,splitter,splitter-QPoint(56,0));
    const int manualLeft=hierarchy->width(),manualRight=inspector->width();
    Check(manualRight>=beforeReorderRight.width()+30,"Cannot establish manually resized sidebar after a dock reorder");
    source=DragSource(host,project);
    DragDock(host,controller,source.Widget,source.Point,DropOnTabBar(host,scene),[&]{checkPreview(0,false);});
    const int centerWidth=VisibleGroup(host,scene).width();
    host->resize(previousWidth+240,host->height());Settle();
    Check(qAbs(hierarchy->width()-manualLeft)<=6&&qAbs(inspector->width()-manualRight)<=6&&VisibleGroup(host,scene).width()>=centerWidth+230,
        "Window growth after docking did not preserve sidebars and expand the center group");
    host->resize(previousWidth,host->height());Settle();

    // Drag a tab outside to float, then use its real title to dock into Scene/Game tabs.
    reset();source=DragSource(host,project);const QPoint outside=host->mapToGlobal(QPoint(host->width()+150,host->height()/2));
    DragDock(host,controller,source.Widget,source.Point,outside,[&]{Check(!controller->property("hasDropTarget").toBool(),"Outside float showed an unrelated drop target");});
    Check(project->isFloating()&&!console->isFloating()&&project->widget()==projectContent,"Dragging tab outside did not detach only that tab");
    source=DragSource(host,project);
    DragDock(host,controller,source.Widget,source.Point,DropOnTabBar(host,scene),[&]{checkPreview(0,false);});
    const auto scenePeers=host->tabifiedDockWidgets(scene);
    Check(!project->isFloating()&&scenePeers.contains(project)&&scenePeers.contains(game),
        "Floating title did not merge with target tab group, floating="+std::to_string(project->isFloating())+
        " hasProject="+std::to_string(scenePeers.contains(project))+" hasGame="+std::to_string(scenePeers.contains(game)));
    Check(project->widget()==projectContent,"Dock drag recreated Project contents");

    // Removing a one-pane group must collapse its empty rectangle.
    reset();ide->QueryDocker("Inspector")->Activate();Settle(30);source=DragSource(host,inspector);
    DragDock(host,controller,source.Widget,source.Point,DropOnTabBar(host,scene),[&]{checkPreview(0,false);});
    Check(host->tabifiedDockWidgets(scene).contains(inspector)&&BlankRegions(host).empty(),"Empty source group was not collapsed after tab merge");

    // Float two real panels, merge into one floating workspace, then split a third
    // panel into the same workspace without breaking its existing tab group.
    const auto floatPanel=[&](QDockWidget* panel) {
        ide->QueryDocker(panel->objectName())->Activate();Settle(30);
        auto* menu=panel->findChild<QMenu*>("DockerContextMenu."+panel->objectName());
        auto* action=menu?menu->findChild<QAction*>("DockerFloat"):nullptr;
        Check(action,"Float command missing for "+panel->objectName().toStdString());
        action->trigger();Settle();
        Check(panel->isFloating(),"Could not create an independent floating Docker");
    };
    const QRect screen=host->screen()->availableGeometry();
    const QPoint origin=screen.topLeft()+QPoint(80,60);
    const auto mergeFloatingPair=[&] {
        project->setGeometry(QRect(origin,QSize(460,360)));
        console->setGeometry(QRect(origin+QPoint(480,0),QSize(340,280)));
        project->raise();Settle();
        const QRect targetRect=GlobalDockRect(project);
        const auto consoleSource=DragSource(host,console);
        DragDock(host,controller,consoleSource.Widget,consoleSource.Point,DropOnTabBar(host,project),[&]{checkPreview(0,false);});
        auto* floating=qobject_cast<QMainWindow*>(project->parentWidget());
        Check(floating&&floating!=host&&floating->isWindow()&&console->parentWidget()==floating&&
            floating->tabifiedDockWidgets(project).contains(console)&&project->widget()==projectContent,
            "Floating tab drop did not preserve both panels in one floating workspace");
        const QPoint floatingOrigin=floating->mapToGlobal(QPoint());
        Check(qAbs(floatingOrigin.x()-targetRect.x())<=6&&qAbs(floatingOrigin.y()-targetRect.y())<=6,
            "Promoting a floating Docker moved the target window");
        return floating;
    };
    reset();
    for(auto* panel:{project,console,hierarchy}) floatPanel(panel);
    hierarchy->setGeometry(QRect(origin+QPoint(480,315),QSize(340,220)));
    auto* floating=mergeFloatingPair();
    const QRect target=VisibleGroup(floating,console);
    const QPoint edge(target.center().x(),target.bottom()-target.height()/6);
    source=DragSource(host,hierarchy);QRect preview;
    DragDock(host,controller,source.Widget,source.Point,floating->mapToGlobal(edge),[&]{
        checkPreview(4,false);preview=controller->property("previewGeometry").toRect();
    });
    Check(hierarchy->parentWidget()==floating&&floating->tabifiedDockWidgets(project).contains(console)&&
        !floating->tabifiedDockWidgets(project).contains(hierarchy)&&hierarchy->y()>VisibleGroup(floating,console).bottom(),
        "Floating edge drop did not split below the existing tab group");
    CheckDropRectangle(preview,hierarchy);

    const int sceneTabY=DropOnTabBar(host,scene).y();
    floating->move(floating->x(),qMax(floating->y(),sceneTabY+60));Settle();
    QPointer<QMainWindow> firstFloating=floating;
    for(auto* panel:{project,console,hierarchy}) {
        source=DragSource(floating,panel);
        DragDock(host,controller,source.Widget,source.Point,DropOnTabBar(host,scene),[&]{checkPreview(0,false);});
        Check(panel->parentWidget()==host&&host->tabifiedDockWidgets(scene).contains(panel),"Docking a floating tab back lost the panel");
    }
    Check(!firstFloating||!firstFloating->isVisible(),"Empty floating workspace remained visible");

    // A native close must keep the Dockers available through the Window menu.
    reset();
    for(auto* panel:{project,console}) floatPanel(panel);
    floating=mergeFloatingPair();
    QPointer<QMainWindow> closedFloating=floating;
    floating->close();Settle();
    Check((!closedFloating||!closedFloating->isVisible())&&project->parentWidget()==host&&console->parentWidget()==host&&
        project->isHidden()&&console->isHidden(),"Closing a floating workspace lost or left its panels visible");
    project->toggleViewAction()->trigger();Settle();
    Check(project->isVisible()&&project->widget()==projectContent,"Window menu could not reopen the original floating pane content");

    reset();
    for(auto* panel:{project,console}) floatPanel(panel);
    floating=mergeFloatingPair();
    QPointer<QMainWindow> hiddenFloating=floating;
    project->toggleViewAction()->trigger();Settle();
    Check(hiddenFloating&&hiddenFloating->isVisible()&&console->isVisible(),"Hiding one tab closed the remaining floating workspace");
    console->toggleViewAction()->trigger();Settle();
    Check((!hiddenFloating||!hiddenFloating->isVisible())&&project->parentWidget()==host&&console->parentWidget()==host&&
        project->isHidden()&&console->isHidden(),"Hiding the last tab left an empty floating workspace");
    reset();Check(host->grab().save("/tmp/gamesprout-dock-final.png"),"Cannot capture final restored dock layout");
}

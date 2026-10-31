#pragma once

static QAction* DockCommand(QDockWidget* dock,const QString& name) {
    auto* menu=dock->findChild<QMenu*>(QStringLiteral("DockerContextMenu.")+dock->objectName());
    auto* action=menu?menu->findChild<QAction*>(name):nullptr;
    Check(action,"Missing docker command: "+name.toStdString());return action;
}
static void CloseDock(QDockWidget* dock) {
    auto* close=DockCommand(dock,QStringLiteral("DockerClose"));
    Check(close->isEnabled(),"Docker Close command is disabled");close->trigger();Settle(50);
    Check(dock->isHidden(),"Docker Close command did not hide "+dock->objectName().toStdString());
}
static void ReopenDockFromWindowMenu(QMainWindow* host,QDockWidget* dock) {
    QAction* action=dock->toggleViewAction();bool registered=false;
    for(auto* menu:host->findChildren<QMenu*>())registered|=menu->actions().contains(action);
    Check(registered&&!action->isChecked(),"Closed docker has no unchecked Window menu action");
    action->trigger();Settle();
    Check(!dock->isHidden()&&action->isChecked(),"Window menu did not reopen "+dock->objectName().toStdString());
}
static void CheckDockRestoreInteractions(IIDEWindow* ide,IDockerLayoutManager* layouts,QMainWindow* host) {
    auto* controller=host->findChild<QObject*>("DockerDragController");Check(controller,"Dock controller missing during restoration checks");
    auto* project=DockWidget(host,"ProjectAssets");auto* console=DockWidget(host,"Console");
    auto* scene=DockWidget(host,"Scene");auto* game=DockWidget(host,"Game");
    auto* hierarchy=DockWidget(host,"Hierarchy");auto* inspector=DockWidget(host,"Inspector");
    const auto reset=[&]{Check(layouts->ResetDefaultLayout(),"Default reset failed before restoration check");Settle();};
    const auto tabsPreview=[&]{Check(controller->property("hasDropTarget").toBool()&&controller->property("dropPosition").toInt()==0&&!controller->property("rootDrop").toBool(),"Restoration gesture did not preview a tab merge");};

    // Closing a tab must not erase its group identity when another group is rebuilt.
    reset();auto* originalBar=Tabs(host,"Project","Console");Check(originalBar,"Original Console group missing");
    const bool consoleAfterProject=TabIndex(originalBar,"Console")>TabIndex(originalBar,"Project");
    QWidget* const consoleContent=console->widget();
    CloseDock(console);
    auto source=DragSource(host,hierarchy);
    DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(VisibleGroup(host,scene).center()),tabsPreview);
    Check(console->isHidden(),"Unrelated drag reopened closed Console");
    ReopenDockFromWindowMenu(host,console);
    auto* reopenedBar=Tabs(host,"Project","Console");
    Check(host->tabifiedDockWidgets(project).contains(console)&&reopenedBar&&
          (TabIndex(reopenedBar,"Console")>TabIndex(reopenedBar,"Project"))==consoleAfterProject,
          "Closed Console lost its original Project group or tab order after unrelated drag");
    Check(console->widget()==consoleContent,"Reopening Console recreated its contents");
    std::cout<<"PASS closed Console restored to Project tab group after unrelated drag"<<std::endl;

    // A closed one-pane split retains the parent/sibling placement, not just an area enum.
    reset();const QRect inspectorBefore=inspector->geometry();QWidget* const inspectorContent=inspector->widget();
    CloseDock(inspector);source=DragSource(host,game);
    DragDock(host,controller,source.Widget,source.Point,host->mapToGlobal(VisibleGroup(host,project).center()),tabsPreview);
    Check(inspector->isHidden(),"Unrelated drag reopened closed Inspector");
    ReopenDockFromWindowMenu(host,inspector);
    QRect remaining;
    for(auto* dock:host->findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(dock==inspector||dock->isFloating()||dock->isHidden()||!host->rect().contains(dock->geometry().center()))continue;
        remaining=remaining.isNull()?dock->geometry():remaining.united(dock->geometry());
    }
    const QRect restored=inspector->geometry();
    std::cout<<"Closed Inspector before="<<Rect(inspectorBefore)<<" restored="<<Rect(restored)<<" others="<<Rect(remaining)<<" sameContent="<<(inspector->widget()==inspectorContent)<<std::endl;
    Check(!inspector->isFloating()&&host->tabifiedDockWidgets(inspector).empty()&&restored.left()>remaining.right()&&
          restored.top()<=DockWorkspace(host).top()+2&&restored.bottom()>=remaining.bottom()-2,
          "Closed Inspector did not return to its original full-height right-hand split");
    Check(qAbs(restored.width()-inspectorBefore.width())<=8&&inspector->widget()==inspectorContent,
          "Closed Inspector lost its split width or recreated contents");
    Check(BlankRegions(host).empty(),"Reopening a closed split reserved an empty rectangle");
    std::cout<<"PASS closed Inspector restored right-hand split width="<<restored.width()<<std::endl;

    // A valid narrow split must survive a transaction elsewhere without a 5% clamp.
    const QSize normalHostSize=host->size();host->resize(4000,800);Settle();reset();
    Check(ide->ResizeDockers({ide->QueryDocker("Hierarchy"),ide->QueryDocker("Scene")},{120,2800},Qt::Horizontal),"Cannot create legal narrow pane fixture");Settle();
    const int narrowBefore=hierarchy->width();
    const int parentWidth=VisibleGroup(host,scene).right()-hierarchy->geometry().left()+1;
    Check(narrowBefore>=hierarchy->minimumSizeHint().width()&&float(narrowBefore)/parentWidth<0.045f,"Fixture could not create a legal pane below 5 percent of its parent span");
    auto* sceneBar=Tabs(host,"Scene","Game");Check(sceneBar,"Narrow pane tab-reorder group missing");
    const QPoint beforeScene=sceneBar->mapToGlobal(QPoint(sceneBar->tabRect(TabIndex(sceneBar,"Scene")).left()+2,sceneBar->height()/2));
    DragDock(host,controller,sceneBar,sceneBar->tabRect(TabIndex(sceneBar,"Game")).center(),beforeScene,tabsPreview);
    std::cout<<"Narrow unrelated-reorder width before="<<narrowBefore<<" after="<<hierarchy->width()<<" parent="<<parentWidth<<std::endl;
    Check(qAbs(hierarchy->width()-narrowBefore)<=8,"Unrelated dock transaction expanded a legal narrow pane to an artificial minimum ratio");
    host->resize(normalHostSize);Settle();reset();

    // The temporary normal layout used for dragging a maximized pane is rolled
    // back on Escape, while a committed drag deliberately keeps the new layout.
    auto* maximize=DockCommand(scene,QStringLiteral("DockerMaximize"));
    const QByteArray normalState=host->saveState(1);
    const QPoint projectCenter=host->mapToGlobal(VisibleGroup(host,project).center());
    maximize->trigger();Settle();const QRect maximizedGeometry=scene->geometry();
    Check(hierarchy->isHidden()&&inspector->isHidden()&&project->isHidden(),"Scene maximize fixture did not hide other panes");
    source=DragSource(host,scene);
    DragDock(host,controller,source.Widget,source.Point,projectCenter,tabsPreview,true);
    Check(hierarchy->isHidden()&&inspector->isHidden()&&project->isHidden()&&scene->geometry()==maximizedGeometry,
          "Escape after dragging maximized Scene did not restore maximization");
    maximize->trigger();Settle();
    Check(host->saveState(1)==normalState,"Cancelling maximized drag lost the original normal dock layout");
    maximize->trigger();Settle();source=DragSource(host,scene);
    DragDock(host,controller,source.Widget,source.Point,projectCenter,tabsPreview);
    Check(!hierarchy->isHidden()&&!inspector->isHidden()&&!project->isHidden()&&host->tabifiedDockWidgets(project).contains(scene),
          "Committing a maximized Scene drag restored maximization over the new layout");
    std::cout<<"PASS maximized Scene drag Escape rollback and committed normal layout"<<std::endl;
    reset();
}

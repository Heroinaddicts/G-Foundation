#pragma once
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QScrollArea>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

// 只经过公开 SDK、真正的 Docker 菜单和 Designer 控件验证节点编辑链路。
static void CheckNodeInspectorFlow(ISceneManager* scenes,ISelectionManager* selection,
    IHierarchyUI* hierarchy,IProjectAssetsUI* projectView,IInspectorUI* inspector,
    QTreeView* hierarchyTree,QTreeView* assetTree,QDockWidget* hierarchyDock,
    QDockWidget* inspectorDock,const QDir& projectRoot,
    const std::function<void()>& tick,const std::function<void(const std::function<bool()>&)>& pump) {
    auto* scene=scenes->GetActiveScene();
    const auto sceneID=scene->GetInstanceID();
    auto* lock=inspectorDock->findChild<QToolButton*>("objectLockButton");
    Check(lock,"Inspector lock control missing");
    const auto inspectedID=[&]() -> std::uint64_t {
        QWidget* form=inspectorDock->findChild<QWidget*>("GameObjectInspectorForm");
        if(!form||!form->isVisible())return 0;
        QLineEdit* field=inspectorDock->findChild<QLineEdit*>("localIDEdit");
        return field?field->text().toULongLong():0;
    };
    const auto property=[&](const std::string& component,const QString& key) {
        QTreeWidget* tree=inspectorDock->findChild<QTreeWidget*>(QString::fromStdString("componentProperties.GameSproutRuntime."+component));
        Check(tree,"Missing component details: "+component);
        if(auto* section=tree->parentWidget()->parentWidget())if(auto* details=section->findChild<QToolButton*>("detailsButton"))details->setChecked(true);
        for(QTreeWidgetItemIterator iterator(tree);*iterator;++iterator) {
            QTreeWidgetItem* item=*iterator;
            if(item->data(0,Qt::UserRole).toString()==key)return item->text(1);
        }
        throw std::runtime_error("Missing component property: "+component+"."+key.toStdString());
    };
    const auto selectNode=[&](std::uint64_t id) {
        QModelIndex index=NodeIndex(hierarchyTree->model(),id);Check(index.isValid(),"Missing selectable Runtime node");
        hierarchyTree->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        pump([&]{return inspectedID()==id;});tick();
        Check(selection->GetSceneObjectSelection()==SceneObjectSelection{sceneID,id}&&selection->GetAssetSelection().empty(),"Hierarchy did not publish exclusive scene identity");
        selection->SetAssetSelection({});
        Check(selection->GetSceneObjectSelection().ObjectID==id,"Empty asset setter cleared node domain");
        Check(!inspectorDock->findChild<QWidget*>("assetHeader")->isVisible()
            && !inspectorDock->findChild<QWidget*>("diagnosticsHost")->isVisible(),
            "Normal node Inspector retained duplicate header or diagnostic rows");
        Check(!inspectorDock->findChild<QLineEdit*>("objectInstanceIDEdit")
            && !inspectorDock->findChild<QLineEdit*>("parentNameEdit"),"Removed object diagnostic controls still exist");
    };
    const auto showMenu=[&](std::uint64_t target,bool keyboard) {
        QModelIndex index=target?NodeIndex(hierarchyTree->model(),target):QModelIndex{};
        if(target) {Check(index.isValid(),"Context target missing");hierarchyTree->scrollTo(index);QCoreApplication::processEvents();}
        else {hierarchyTree->collapseAll();QCoreApplication::processEvents();}
        if(keyboard)hierarchyTree->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        const QPoint local=index.isValid()?hierarchyTree->visualRect(index).center():QPoint(hierarchyTree->viewport()->width()-3,hierarchyTree->viewport()->height()-3);
        Check(target||!hierarchyTree->indexAt(local).isValid(),"Blank context fixture hit a row");
        QContextMenuEvent event(keyboard?QContextMenuEvent::Keyboard:QContextMenuEvent::Mouse,local,hierarchyTree->viewport()->mapToGlobal(local));
        QCoreApplication::sendEvent(hierarchyTree->viewport(),&event);
        QCoreApplication::processEvents();
        QMenu* menu=hierarchyDock->findChild<QMenu*>("Hierarchy.ContentContextMenu");
        Check(menu&&menu->isVisible(),"Real Hierarchy context popup missing");
        return menu;
    };
    const auto create=[&](const QString& item,bool child,std::uint64_t context,bool keyboard=false) {
        std::cerr<<"Node Inspector create "<<(child?"child ":"root ")<<item.toStdString()<<'\n';
        QMenu* menu=showMenu(context,keyboard);
        QMenu* submenu=MenuAction(menu,child?"Create Child (Context Node)":"Create (Scene Root)")->menu();
        QAction* action=MenuAction(submenu,item);Check(action->isEnabled(),"Node creation menu disabled");
        action->trigger();submenu->hide();menu->hide();
        const auto chosen=selection->GetSceneObjectSelection();
        Check(chosen.SceneID==sceneID&&chosen.ObjectID&&selection->GetAssetSelection().empty(),"Creation did not select new Runtime object");
        auto* object=scenes->GetRuntime()->QueryGameObject(chosen.ObjectID);
        Check(object&&object->GetScene()==scene&&object->Parent()==(child&&context?scenes->GetRuntime()->QueryGameObject(context):scene),"Create/Child used wrong parent");
        pump([&]{return inspectedID()==chosen.ObjectID;});tick();
        QModelIndex index=NodeIndex(hierarchyTree->model(),chosen.ObjectID);
        Check(index.isValid()&&hierarchy->GetSelectedObjectID()==chosen.ObjectID&&hierarchyTree->currentIndex()==index,"Created node not selected in tree");
        for(QModelIndex ancestor=index.parent();ancestor.isValid();ancestor=ancestor.parent())Check(hierarchyTree->isExpanded(ancestor),"Created node ancestor not expanded");
        return object;
    };
    struct Preset {const char* Menu;const char* Name;std::vector<std::string> Components;};
    const std::vector<Preset> presets={
        {"Empty","GameObject",{}},{"Camera","Camera",{"Camera"}},
        {"Mesh","Mesh",{"Mesh","MeshRenderer"}},
        {"UI Canvas","Canvas",{"RectTransform","Canvas"}},
        {"UI Image","Image",{"RectTransform","Image"}},
        {"UI Empty","UI Object",{"RectTransform"}}
    };
    const std::map<std::string,std::vector<QString>> fields={
        {"Transform",{"LocalPosition","LocalRotation","LocalScale","LocalMatrix","WorldMatrix"}},
        {"RectTransform",{"AnchorMin","AnchorMax","Pivot","AnchoredPosition","SizeDelta"}},
        {"Camera",{"Projection","VerticalFovDegrees","NearPlane","FarPlane","CullingMask","MainCamera"}},
        {"Mesh",{"MeshGuid","MeshLocalID","BoundsCenter","BoundsExtents"}},
        {"MeshRenderer",{"Visible","MaterialCount"}},
        {"Canvas",{"RenderMode","ReferenceResolution","PixelsPerUnit"}},
        {"Image",{"TextureGuid","TextureLocalID","Color","UVPosition","UVSize"}}
    };
    const auto checkDetails=[&](GameSproutRuntime::IGameObject* object,const Preset& preset) {
        Check(object->GetName()==preset.Name&&object->GetComponentCount()==1+preset.Components.size(),"Created preset name/components incorrect");
        QLineEdit* name=inspectorDock->findChild<QLineEdit*>("objectNameEdit");
        QLineEdit* owner=inspectorDock->findChild<QLineEdit*>("guidEdit");
        Check(name&&owner&&!name->isReadOnly()&&name->text()==preset.Name&&owner->text().toULongLong()==sceneID,
            "Compact Inspector name or internal scene binding incorrect");
        std::vector<std::string> types=preset.Components;types.insert(types.begin(),"Transform");
        for(const std::string& type:types)for(const QString& field:fields.at(type))Check(!property(type,field).isNull(),"Component property value missing");
        Check(!inspector->HasPendingChanges(),"Readonly node inspector reported asset draft");
    };
    std::vector<std::uint64_t> roots;
    for(const Preset& preset:presets) {
        auto* object=create(preset.Menu,false,sceneID);roots.push_back(object->GetInstanceID());checkDetails(object,preset);
    }
    auto* parent=scenes->GetRuntime()->QueryGameObject(roots.front());
    for(const Preset& preset:presets)checkDetails(create(preset.Menu,true,parent->GetInstanceID(),preset.Menu==std::string("UI Empty")),preset);
    auto* duplicate=create("Empty",true,parent->GetInstanceID());
    Check(duplicate->GetName()=="GameObject (1)","Sibling duplicate name not made unique");
    const auto duplicateID=duplicate->GetInstanceID();
    // 鼠标空白不沿用上次选中目标；Create 固定根，即使右键的是子节点。
    auto* blankChild=create("Empty",true,0);roots.push_back(blankChild->GetInstanceID());
    Check(blankChild->Parent()==scene&&blankChild->GetName()=="GameObject (1)","Blank context reused selected parent");
    auto* fixedRoot=create("Empty",false,duplicateID);roots.push_back(fixedRoot->GetInstanceID());
    Check(fixedRoot->Parent()==scene&&fixedRoot->GetName()=="GameObject (2)","Create did not target scene root");
    // 弹出后删除右键目标：不可默默退回到场景根创建。
    auto* stale=scene->CreateGameObject("Stale Context");tick();
    QMenu* staleMenu=showMenu(stale->GetInstanceID(),false);
    QAction* staleCreate=MenuAction(MenuAction(staleMenu,"Create Child (Context Node)")->menu(),"Empty");
    Check(scene->DestroyGameObject(stale),"Cannot remove context node");
    const auto beforeStale=scene->GetChildCount();staleCreate->trigger();staleMenu->hide();tick();
    Check(scene->GetChildCount()==beforeStale,"Stale context unexpectedly created root object");

    selectNode(parent->GetInstanceID());
    parent->SetName("Inspectable Parent");parent->GetTransform()->SetLocalPosition({2,3,4});parent->SetActive(false);
    pump([&]{return inspectorDock->findChild<QLineEdit*>("objectNameEdit")->text()=="Inspectable Parent"&&property("Transform","LocalPosition").contains("2");});
    auto* inherited=parent->GetChild(0);selectNode(inherited->GetInstanceID());
    Check(!inherited->IsActiveInHierarchy()&&!NodeIndex(hierarchyTree->model(),inherited->GetInstanceID()).data(Qt::UserRole+2).toBool(),"Inactive parent did not propagate");
    auto* activeSelf=inspectorDock->findChild<QCheckBox*>("objectActiveCheck");
    Check(activeSelf&&activeSelf->isChecked()&&!inspectorDock->findChild<QLineEdit*>("hierarchyActiveEdit"),
        "Compact Inspector lost local active state or retained a derived-state row");
    const QString previousWorldRow=property("Transform","WorldMatrix.Row0");
    parent->GetTransform()->SetLocalPosition({5,6,7});
    pump([&]{return property("Transform","WorldMatrix.Row0")!=previousWorldRow;});
    parent->SetActive(true);selectNode(parent->GetInstanceID());
    auto* added=parent->AddComponent<GameSproutRuntime::IImageComponent>();Check(added,"Cannot add Image component");
    pump([&]{return inspectorDock->findChild<QTreeWidget*>("componentProperties.GameSproutRuntime.Image")!=nullptr;});
    const GameSproutRuntime::ResourceReference texture{"00112233445566778899aabbccddeeff",42};
    added->SetTexture(texture);added->SetColor({0.25f,0.5f,0.75f,1});added->SetUVRect({{0.1f,0.2f},{0.4f,0.6f}});
    pump([&]{return property("Image","TextureGuid")==QString::fromStdString(texture.Guid)&&property("Image","TextureLocalID")=="42";});
    Check(property("Image","Color").contains("0.25")&&property("Image","UVSize").contains("0.6"),"Component field event did not refresh details");
    Check(parent->RemoveComponent<GameSproutRuntime::IImageComponent>(),"Cannot remove added component");
    pump([&]{return !inspectorDock->findChild<QTreeWidget*>("componentProperties.GameSproutRuntime.Image");});
    Check(!parent->RemoveComponent<GameSproutRuntime::ITransformComponent>(),"Core Transform removal allowed");
    auto* renderer=scenes->GetRuntime()->QueryGameObject(roots[2])->GetComponent<GameSproutRuntime::IMeshRendererComponent>();
    Check(renderer->SetMaterial(0,texture),"Cannot assign material slot");renderer->SetVisible(false);selectNode(roots[2]);
    Check(property("MeshRenderer","MaterialCount")=="1"&&property("MeshRenderer","Material.0.Guid")==QString::fromStdString(texture.Guid)&&property("MeshRenderer","Material.0.LocalID")=="42","Renderer material slot details missing");
    auto* mesh=scenes->GetRuntime()->QueryGameObject(roots[2])->GetComponent<GameSproutRuntime::IMeshComponent>();
    mesh->SetMesh(texture);
    pump([&]{return property("Mesh","MeshGuid")==QString::fromStdString(texture.Guid)&&property("Mesh","MeshLocalID")=="42";});
    auto* camera=scenes->GetRuntime()->QueryGameObject(roots[1])->GetComponent<GameSproutRuntime::ICameraComponent>();
    auto cameraSettings=camera->GetSettings();cameraSettings.VerticalFovRadians=GameSproutRuntime::DegreesToRadians(75);cameraSettings.NearPlane=0.25f;
    Check(camera->SetSettings(cameraSettings),"Cannot update camera settings");selectNode(roots[1]);
    std::cerr<<"Camera Inspector FOV="<<property("Camera","VerticalFovDegrees").toStdString()<<" Near="<<property("Camera","NearPlane").toStdString()<<'\n';
    Check(std::abs(property("Camera","VerticalFovDegrees").toDouble()-75.0)<0.001&&property("Camera","NearPlane")=="0.25","Camera settings details did not update");
    auto* canvas=scenes->GetRuntime()->QueryGameObject(roots[3])->GetComponent<GameSproutRuntime::ICanvasComponent>();
    Check(canvas->SetReferenceResolution({1280,720})&&canvas->SetPixelsPerUnit(200),"Cannot update Canvas settings");
    auto* rect=scenes->GetRuntime()->QueryGameObject(roots[3])->GetComponent<GameSproutRuntime::IRectTransformComponent>();
    rect->SetPivot({0.25f,0.75f});rect->SetSizeDelta({320,180});selectNode(roots[3]);
    Check(property("Canvas","ReferenceResolution").contains("1280")&&property("Canvas","PixelsPerUnit")=="200"&&property("RectTransform","Pivot").contains("0.25")&&property("RectTransform","SizeDelta").contains("320"),"Canvas/RectTransform details did not update");

    // 场景根也是合法检查目标，节点和资产选择在两侧刷新后保持互斥。
    selectNode(sceneID);Check(NodeIndex(hierarchyTree->model(),sceneID).data(Qt::UserRole+3).toBool(),"Scene root identity missing");
    Check(!activeSelf->isVisible(),"Scene root displayed a GameObject active switch");
    QWidget* rootForm=inspectorDock->findChild<QWidget*>("GameObjectInspectorForm");
    Check(rootForm->findChild<QWidget*>("objectHeader")->height()<=50
        && rootForm->findChild<QScrollArea*>("componentScroll")->y()<70,
        "Scene root header still consumes space reserved for component properties");
    if(auto* transform=rootForm->findChild<QWidget*>("GameObjectComponentSection.GameSproutRuntime.Transform"))
        Check(!transform->isVisible(),"Scene root displayed its fixed internal Transform in Normal mode");
    Check(!rootForm->findChild<QScrollArea*>("componentScroll")->isVisible(),"Scene root retained an empty component scrollbar");
    Check(inspectorDock->grab().save("/tmp/gamesprout-scene-root-inspector.png"),"Cannot capture compact Scene root Inspector");
    Check(projectView->NavigateToDirectory(""),"Cannot navigate project root for selection test");tick();
    const auto chooseAsset=[&] {
        const QModelIndex index=FindRow(assetTree,"Sample.png");Check(index.isValid(),"Selection fixture missing");
        assetTree->selectionModel()->setCurrentIndex(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        pump([&]{return inspectedID()==0&&inspectorDock->findChild<QLabel*>("assetNameLabel")->text()=="Sample.png";});
        Check(selection->GetSceneObjectSelection()==SceneObjectSelection{}&&selection->GetAssetSelection().size()==1,"Asset selection retained scene target");
        selection->SetSceneObjectSelection({});
        Check(selection->GetAssetSelection().size()==1,"Empty node setter cleared asset domain");
    };
    chooseAsset();
    auto* maxSize=inspectorDock->findChild<QComboBox*>("maxSizeCombo");Check(maxSize,"Texture draft control missing");
    maxSize->setCurrentText("8");Check(inspector->HasPendingChanges(),"Texture draft not detected before node switch");
    QTimer draftResponse;QString response="cancelButton";bool responded=false;
    QObject::connect(&draftResponse,&QTimer::timeout,hierarchyDock,[&] {
        if(QWidget* dialog=QApplication::activeModalWidget())if(auto* button=dialog->findChild<QPushButton*>(response)) {
            draftResponse.stop();responded=true;button->click();
        }
    });
    draftResponse.start(5);
    const QModelIndex draftTarget=NodeIndex(hierarchyTree->model(),roots[4]);
    hierarchyTree->selectionModel()->setCurrentIndex(draftTarget,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);tick();tick();
    Check(responded&&inspector->HasPendingChanges()&&selection->GetAssetSelection().size()==1&&selection->GetSceneObjectSelection()==SceneObjectSelection{}&&inspectedID()==0&&hierarchy->GetSelectedObjectID()==0,"Cancel asset draft/node switch did not restore asset domain");
    response="discardButton";responded=false;draftResponse.start(5);selectNode(roots[4]);draftResponse.stop();
    Check(responded&&!inspector->HasPendingChanges(),"Discard asset draft/node switch did not inspect node");
    std::string error;
    Check(AssetArtifactIO::Write(projectRoot.filePath("Assets/SelectionRefresh.txt"),"Force asset model refresh",error),error);
    pump([&]{return FindRow(assetTree,"SelectionRefresh.txt").isValid();});
    Check(selection->GetSceneObjectSelection().ObjectID==roots[4]&&inspectedID()==roots[4]&&selection->GetAssetSelection().empty(),"Asset refresh stole node selection");
    chooseAsset();parent->SetName("Refresh While Asset Selected");tick();tick();
    Check(selection->GetAssetSelection().size()==1&&hierarchy->GetSelectedObjectID()==0&&inspectedID()==0,"Hierarchy refresh stole asset selection");
    selectNode(roots[4]);lock->setChecked(true);tick();
    const auto lockedID=roots[4];
    const QModelIndex other=NodeIndex(hierarchyTree->model(),roots[1]);
    hierarchyTree->selectionModel()->setCurrentIndex(other,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);tick();tick();
    Check(inspectedID()==lockedID&&selection->GetSceneObjectSelection().ObjectID==roots[1],"Inspector lock changed selection or lost target");
    Check(scene->DestroyGameObject(scenes->GetRuntime()->QueryGameObject(lockedID)),"Cannot delete locked node");
    pump([&]{return inspectedID()==0;});
    Check(lock->isChecked(),"Deleted locked node unexpectedly unlocked Inspector");
    const QModelIndex whileLocked=NodeIndex(hierarchyTree->model(),roots.front());
    hierarchyTree->selectionModel()->setCurrentIndex(whileLocked,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);tick();tick();
    Check(inspectedID()==0&&selection->GetSceneObjectSelection().ObjectID==roots.front(),"Deleted locked node followed a later selection before unlock");
    lock->setChecked(false);pump([&]{return inspectedID()==roots.front();});
    selectNode(roots[1]);
    Check(scene->DestroyGameObject(scenes->GetRuntime()->QueryGameObject(roots[1])),"Cannot delete selected node");
    pump([&]{return selection->GetSceneObjectSelection()==SceneObjectSelection{}&&inspectedID()==0&&hierarchy->GetSelectedObjectID()==0;});
    selectNode(roots[2]);
    QAction* normalMode=nullptr;QAction* debugMode=nullptr;
    for (QMenu* menu:inspectorDock->findChildren<QMenu*>()) for (QAction* action:menu->actions()) {
        if(action->text()==QStringLiteral("普通模式"))normalMode=action;
        if(action->text()==QStringLiteral("调试模式"))debugMode=action;
    }
    Check(normalMode&&debugMode&&debugMode->isEnabled()&&!normalMode->isEnabled(),"Inspector mode menu missing or wrong default");
    debugMode->trigger();QCoreApplication::processEvents();
    Check(inspectorDock->findChild<QWidget*>("diagnosticsHost")->isVisible(),"Debug mode did not expose object identity");
    normalMode->trigger();QCoreApplication::processEvents();
    Check(!inspectorDock->findChild<QWidget*>("diagnosticsHost")->isVisible(),"Normal mode retained debug identity");
    for(QToolButton* details:inspectorDock->findChildren<QToolButton*>("detailsButton"))
        Check(!details->isVisible(),"Normal component Inspector retained a diagnostic row");
    hierarchyDock->setFloating(true);hierarchyDock->resize(360,850);hierarchyDock->show();
    inspectorDock->setFloating(true);inspectorDock->resize(620,1050);inspectorDock->show();QCoreApplication::processEvents();
    QWidget* transformSection=inspectorDock->findChild<QWidget*>("GameObjectComponentSection.GameSproutRuntime.Transform");
    Check(transformSection,"Screenshot Transform section missing");
    auto* collapseTransform=transformSection->findChild<QToolButton*>("componentHeaderButton");Check(collapseTransform,"Component collapse control missing");
    collapseTransform->setChecked(false);QCoreApplication::processEvents();
    for(int pass=0;pass<3;++pass) {
        QCoreApplication::sendPostedEvents(nullptr,QEvent::LayoutRequest);QCoreApplication::processEvents();
    }
    Check(transformSection->height()<=collapseTransform->height()+16,"Collapsed component retained a large empty block");
    QPixmap preview(hierarchyDock->width()+inspectorDock->width(),std::max(hierarchyDock->height(),inspectorDock->height()));preview.fill(QColor("#25272b"));
    QPainter painter(&preview);painter.drawPixmap(0,0,hierarchyDock->grab());painter.drawPixmap(hierarchyDock->width(),0,inspectorDock->grab());painter.end();
    Check(preview.save("/tmp/gamesprout-node-inspector-preview.png"),"Node Inspector screenshot failed");
    hierarchyDock->setFloating(false);inspectorDock->setFloating(false);
    for(std::uint64_t id:roots)if(auto* object=scenes->GetRuntime()->QueryGameObject(id))Check(scene->DestroyGameObject(object),"Cannot clean up created node");
    selection->SetSceneObjectSelection({});tick();tick();
}

#include "SceneInteractionFixture.h"
#include "SceneViewportEditing.h"
#include <QContextMenuEvent>
#include <QMenu>

struct DeletionTree {
    GameSproutRuntime::IGameObject* Before=nullptr;
    GameSproutRuntime::IGameObject* Parent=nullptr;
    GameSproutRuntime::IGameObject* Child=nullptr;
    GameSproutRuntime::IGameObject* Grandchild=nullptr;
    GameSproutRuntime::IGameObject* After=nullptr;
};
static DeletionTree CreateDeletionTree(ISceneManager* scenes,bool renderable) {
    auto* scene=scenes->GetActiveScene();DeletionTree result;
    result.Before=scene->CreateGameObject("Before subtree");result.Parent=scene->CreateGameObject("Delete subtree");
    if(!renderable) {
        result.Parent->GetTransform()->SetLocalPosition({1.25f,-2,3});result.Parent->GetTransform()->SetLocalScale({-1,2,.5f});
        Check(result.Parent->GetTransform()->SetLocalRotation({.13f,.27f,.39f,.81f}),"Cannot pose deletion parent");result.Parent->SetActive(false);
    }
    scene->CreateGameObject("First nested sibling",result.Parent);
    result.Child=scene->CreateGameObject("All components",result.Parent);
    if(!renderable)result.Child->GetTransform()->SetLocalPosition({.5f,.25f,-1});
    auto* rect=result.Child->AddComponent<GameSproutRuntime::IRectTransformComponent>();
    rect->SetAnchors({.1f,.2f},{.8f,.9f});rect->SetPivot({.3f,.7f});rect->SetAnchoredPosition({10,20});rect->SetSizeDelta({320,180});
    auto* image=result.Child->AddComponent<GameSproutRuntime::IImageComponent>();image->SetTexture({"00112233445566778899aabbccddeeff",42});image->SetColor({.3f,.4f,.5f,.6f});image->SetUVRect({{.1f,.2f},{.7f,.8f}});
    auto* canvas=result.Child->AddComponent<GameSproutRuntime::ICanvasComponent>();canvas->SetRenderMode(GameSproutRuntime::CanvasRenderMode::WorldSpace);
    Check(canvas->SetReferenceResolution({1440,900})&&canvas->SetPixelsPerUnit(128),"Cannot configure deleted Canvas");
    result.Child->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"11223344556677889900aabbccddeeff",1});
    auto* renderer=result.Child->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
    Check(renderer->SetMaterials({{"ffeeddccbbaa00998877665544332211",1},{}}),"Cannot configure deleted materials");renderer->SetVisible(renderable);
    auto* camera=result.Child->AddComponent<GameSproutRuntime::ICameraComponent>();auto settings=camera->GetSettings();
    settings.Projection=GameSproutRuntime::CameraProjection::Orthographic;settings.OrthographicHeight=12;settings.NearPlane=.25f;settings.FarPlane=200;settings.CullingMask=0xAABBu;
    Check(camera->SetSettings(settings)&&scene->SetMainCamera(camera),"Cannot configure deleted MainCamera");
    result.Grandchild=scene->CreateGameObject("Grandchild",result.Child);result.Grandchild->GetTransform()->SetLocalScale({.5f,1.5f,-2});
    scene->CreateGameObject("Last nested sibling",result.Parent);result.After=scene->CreateGameObject("After subtree");return result;
}
static Json::Value SavedDocument(SceneEditingServices& fixture,const QString& name="Deletion.scene") {
    Json::Value result;Check(AssetArtifactIO::Parse(fixture.ReadScene(name).toStdString(),result),"Saved deletion document is invalid");return result;
}
static std::vector<std::string> ChildNames(GameSproutRuntime::IGameObject* parent) {
    std::vector<std::string> result;for(std::size_t index=0;index<parent->GetChildCount();++index)result.push_back(parent->GetChild(index)->GetName());return result;
}
static std::uint64_t FileID(const Json::Value& document,const char* name) {
    for(const auto& object:document["Objects"])if(object["Name"]==name)return object["ID"].asUInt64();return 0;
}
static void CheckRestoredComponents(GameSproutRuntime::IGameObject& parent,GameSproutRuntime::IGameObject& child,GameSproutRuntime::IScene& scene,bool renderable) {
    Check(parent.IsActiveSelf()==renderable&&child.IsActiveSelf()&&child.GetComponentCount()==7,"Deletion Undo lost component count or active state");
    auto* rect=child.GetComponent<GameSproutRuntime::IRectTransformComponent>();auto* image=child.GetComponent<GameSproutRuntime::IImageComponent>();
    auto* canvas=child.GetComponent<GameSproutRuntime::ICanvasComponent>();auto* mesh=child.GetComponent<GameSproutRuntime::IMeshComponent>();
    auto* renderer=child.GetComponent<GameSproutRuntime::IMeshRendererComponent>();auto* camera=child.GetComponent<GameSproutRuntime::ICameraComponent>();
    Check(rect&&image&&canvas&&mesh&&renderer&&camera,"Deletion Undo omitted a built-in component");
    Check(rect->GetAnchorMin().X==.1f&&rect->GetAnchorMax().Y==.9f&&rect->GetSizeDelta().X==320&&image->GetTexture().LocalID==42&&image->GetColor().A==.6f,"Deletion Undo lost RectTransform/Image properties");
    Check(canvas->GetRenderMode()==GameSproutRuntime::CanvasRenderMode::WorldSpace&&canvas->GetPixelsPerUnit()==128&&mesh->GetMesh().LocalID==1&&renderer->GetMaterialCount()==2&&renderer->GetMaterial(1).Guid.empty()&&renderer->IsVisible()==renderable,"Deletion Undo lost Canvas/Mesh/material properties");
    Check(camera->GetSettings().Projection==GameSproutRuntime::CameraProjection::Orthographic&&camera->GetSettings().CullingMask==0xAABBu&&scene.GetMainCamera()==camera,"Deletion Undo failed to restore MainCamera");
    Check(ChildNames(&parent)==std::vector<std::string>{"First nested sibling","All components","Last nested sibling"}&&child.FindChild("Grandchild"),"Deletion Undo lost nested sibling order or grandchildren");
}
static void CheckModuleDeletion() {
    SceneEditingServices fixture;fixture.Initialize();auto* scenes=fixture.Scenes;auto* scene=scenes->GetActiveScene();auto* runtime=scenes->GetRuntime();
    const auto sceneID=scene->GetInstanceID();auto* defaultCameraObject=scene->GetMainCamera()->GetGameObject();const auto defaultCameraID=defaultCameraObject->GetInstanceID();
    auto tree=CreateDeletionTree(scenes,false);const auto beforeID=tree.Before->GetInstanceID(),afterID=tree.After->GetInstanceID();
    const auto parentID=tree.Parent->GetInstanceID(),childID=tree.Child->GetInstanceID(),grandID=tree.Grandchild->GetInstanceID();
    const auto originalPosition=tree.Child->GetTransform()->GetLocalPosition();const GameSproutRuntime::Vector3 editedPosition{2,4,-3};
    const auto originalSiblingOrder=ChildNames(scene);std::string error;
    Check(scenes->SaveSceneAs("Scenes/Deletion.scene",error),error);const auto originalDocument=SavedDocument(fixture);const auto childFileID=scenes->GetObjectFileID(childID);
    Check(childFileID&&scenes->QueryObjectByFileID(childFileID)==tree.Child,"Stable FileID lookup did not resolve the live object");
    Check(!scenes->DeleteObject(0,error)&&!scenes->DeleteObject(sceneID,error)&&!scenes->DeleteObject(std::numeric_limits<uint64_t>::max(),error)&&!scenes->CanUndo(),"Invalid/root deletion altered the scene or history");
    auto* otherScene=runtime->CreateScene("Other");auto* other=otherScene->CreateGameObject("Other scene object");
    Check(!scenes->DeleteObject(other->GetInstanceID(),error)&&runtime->DestroyScene(otherScene),"Delete accepted an object from a different scene");
    bool callbackAccepted=false;scene->ForeachChild([&](auto*){callbackAccepted|=scenes->DeleteObject(parentID,error);});
    Check(!callbackAccepted&&runtime->QueryGameObject(parentID),"Delete entered a Runtime iteration callback");
    Check(scenes->BeginTransformEdit(childID,"Delete guard",error),error);auto preview=ReadTransform(*tree.Child);preview.Position={8,9,10};Check(scenes->PreviewTransformEdit(preview,error),error);
    Check(!scenes->DeleteObject(parentID,error)&&scenes->HasTransformEdit()&&runtime->QueryGameObject(childID),"Delete implicitly cancelled an active gizmo or deleted its target");
    Check(scenes->CancelTransformEdit(error),error);
    Check(scenes->EditObject(childID,"Old transform history",[editedPosition](auto& object,std::string&){object.GetTransform()->SetLocalPosition(editedPosition);return true;},
        [originalPosition](auto& object,std::string&){object.GetTransform()->SetLocalPosition(originalPosition);return true;},error),error);
    Check(scenes->SaveScene(error),error);const auto editedDocument=SavedDocument(fixture);
    fixture.Selection->SetSceneObjectSelection({sceneID,grandID});fixture.Tick();
    Check(scenes->DeleteObject(parentID,error),error);fixture.Tick();
    Check(scenes->GetActiveScene()==scene&&runtime->QueryGameObject(sceneID)==scene&&runtime->QueryGameObject(defaultCameraID)==defaultCameraObject&&runtime->QueryGameObject(beforeID)==tree.Before&&runtime->QueryGameObject(afterID)==tree.After,"Deletion replaced the scene or unrelated Runtime objects");
    Check(!runtime->QueryGameObject(parentID)&&!runtime->QueryGameObject(childID)&&!runtime->QueryGameObject(grandID)&&!scene->GetMainCamera()&&fixture.Selection->GetSceneObjectSelection().ObjectID==0,"Deleting a subtree left descendants, MainCamera or selection alive");
    Check(scenes->SaveScene(error),error);const auto deletedDocument=SavedDocument(fixture);Check(!FileID(deletedDocument,"Delete subtree")&&!FileID(deletedDocument,"All components"),"Deleted subtree was still serialized");
    Check(scenes->Undo(error),error);fixture.Tick();
    auto* restoredParent=scene->FindChild("Delete subtree");auto* restored=restoredParent?restoredParent->FindChild("All components"):nullptr;
    Check(restored&&restoredParent->GetInstanceID()!=parentID&&restored->GetInstanceID()!=childID&&ChildNames(scene)==originalSiblingOrder,"Undo did not recreate the subtree at its original sibling position");
    Check(scenes->GetObjectFileID(restored->GetInstanceID())==childFileID&&scenes->QueryObjectByFileID(childFileID)==restored,"Undo did not rebind the original FileID to the new Runtime object");
    CheckRestoredComponents(*restoredParent,*restored,*scene,false);Check(CloseVector(restored->GetTransform()->GetLocalPosition(),editedPosition),"Undo deletion lost the latest transform");
    Check(scenes->SaveScene(error)&&SavedDocument(fixture)==editedDocument,"Undo deletion changed stable FileIDs or component serialization");
    Check(scenes->Undo(error)&&CloseVector(restored->GetTransform()->GetLocalPosition(),originalPosition)&&!scenes->CanUndo(),"Old transform Undo could not resolve the recreated Runtime object by stable FileID");
    Check(scenes->SaveScene(error)&&SavedDocument(fixture)==originalDocument,"Undo history after deletion changed the original saved document");
    Check(scenes->Redo(error)&&CloseVector(restored->GetTransform()->GetLocalPosition(),editedPosition)&&scenes->Redo(error),"Old transform Redo or deletion Redo failed");fixture.Tick();
    Check(!scene->FindChild("Delete subtree")&&!scene->GetMainCamera()&&runtime->QueryGameObject(beforeID)==tree.Before&&runtime->QueryGameObject(afterID)==tree.After,"Redo deletion affected unrelated objects or retained the subtree");
    Check(scenes->SaveScene(error)&&SavedDocument(fixture)==deletedDocument,error);
    Check(scenes->OpenScene("Scenes/Deletion.scene",error),error);fixture.Tick();
    Check(!scenes->GetActiveScene()->FindChild("Delete subtree")&&!scenes->GetActiveScene()->GetMainCamera()&&!scenes->HasUnsavedChanges(),"Saved deletion did not survive scene reopen");
    Check(scenes->SaveScene(error)&&SavedDocument(fixture)==deletedDocument,"Reopening a deleted scene changed its stable FileIDs");
    std::cout<<"PASS real module subtree deletion, stable FileIDs, component/MainCamera/order restore and old transform history\n";
}

static QModelIndex DeletionNodeIndex(QAbstractItemModel* model,uint64_t id,const QModelIndex& parent={}) {
    for(int row=0;row<model->rowCount(parent);++row){const auto index=model->index(row,0,parent);if(index.data(Qt::UserRole+1).toULongLong()==id)return index;const auto nested=DeletionNodeIndex(model,id,index);if(nested.isValid())return nested;}return {};
}
static QAction* DeleteMenu(SceneInteractionFixture& fixture,QTreeView* tree,uint64_t objectID) {
    tree->expandAll();const auto index=DeletionNodeIndex(tree->model(),objectID);QPoint local;
    if(objectID){Check(index.isValid(),"Context deletion target missing");tree->scrollTo(index);fixture.Settle();local=tree->visualRect(index).center();}
    else {local={tree->viewport()->width()-3,tree->viewport()->height()-3};Check(!tree->indexAt(local).isValid(),"Blank context deletion fixture hit an object row");}
    QContextMenuEvent event(QContextMenuEvent::Mouse,local,tree->viewport()->mapToGlobal(local));QCoreApplication::sendEvent(tree->viewport(),&event);QCoreApplication::processEvents();
    auto* menu=fixture.Host->findChild<QDockWidget*>("Hierarchy")->findChild<QMenu*>("Hierarchy.ContentContextMenu");Check(menu&&menu->isVisible(),"Real Hierarchy context menu missing");
    for(auto* action:menu->actions())if(action->text()=="Delete")return action;
    throw std::runtime_error("Hierarchy .ui Delete action is not registered");
}
static void CloseHierarchyMenu(SceneInteractionFixture& fixture) {
    if(auto* menu=fixture.Host->findChild<QDockWidget*>("Hierarchy")->findChild<QMenu*>("Hierarchy.ContentContextMenu"))menu->hide();fixture.Settle();
}
static void CheckViewsDeletion(SceneInteractionFixture& fixture) {
    auto* scenes=fixture.Services.Scenes;auto* runtime=scenes->GetRuntime();auto* scene=scenes->GetActiveScene();std::string error;
    auto tree=CreateDeletionTree(scenes,true);auto* cameraA=scene->AddComponent<GameSproutRuntime::ICameraComponent>();
    Check(scene->SetMainCamera(cameraA),"Cannot initialize MainCamera history baseline");fixture.Select(tree.Child);fixture.Pump([&]{return fixture.Viewport->GetStatistics().VisibleObjects>0;});
    auto* hierarchy=static_cast<IHierarchyUI*>(fixture.Services.Editor.Views["HierarchyUI"]);auto* hierarchyDock=fixture.Host->findChild<QDockWidget*>("Hierarchy");auto* nodes=hierarchyDock->findChild<QTreeView*>("hierarchyTree");Check(nodes,"Real Hierarchy tree missing");
    const auto parentID=tree.Parent->GetInstanceID(),childID=tree.Child->GetInstanceID(),grandID=tree.Grandchild->GetInstanceID();
    Check(scenes->SaveSceneAs("Scenes/ViewDeletion.scene",error),error);
    auto* mainButton=fixture.InspectorDock->findChild<QPushButton*>("setMainCameraButton");Check(mainButton&&mainButton->isEnabled(),"Inspector Set MainCamera button unavailable");mainButton->click();fixture.Settle();
    Check(scene->GetMainCamera()==tree.Child->GetComponent<GameSproutRuntime::ICameraComponent>(),"Inspector did not assign Camera B before deletion");
    DeleteMenu(fixture,nodes,0)->trigger();CloseHierarchyMenu(fixture);Check(runtime->QueryGameObject(parentID)&&runtime->QueryGameObject(childID),"Blank context deleted the previously selected object");
    auto* action=DeleteMenu(fixture,nodes,parentID);Check(action->isEnabled(),"Valid Hierarchy Delete action disabled");action->trigger();CloseHierarchyMenu(fixture);
    fixture.Pump([&]{return !runtime->QueryGameObject(parentID)&&fixture.Services.Selection->GetSceneObjectSelection().ObjectID==0&&fixture.Viewport->GetStatistics().VisibleObjects==0&&fixture.Viewport->GetStatistics().Triangles==0;});
    Check(!DeletionNodeIndex(nodes->model(),parentID).isValid()&&!DeletionNodeIndex(nodes->model(),childID).isValid()&&!DeletionNodeIndex(nodes->model(),grandID).isValid()&&hierarchy->GetSelectedObjectID()==0,"Hierarchy retained deleted subtree rows or selection");
    auto* form=fixture.InspectorDock->findChild<QWidget*>("GameObjectInspectorForm");Check(!form||!form->isVisible(),"Inspector retained a destroyed component target");
    Check(scenes->Undo(error),error);fixture.Settle();auto* restoredParent=scene->FindChild("Delete subtree");auto* restored=restoredParent?restoredParent->FindChild("All components"):nullptr;Check(restored,"View deletion Undo lost subtree");
    CheckRestoredComponents(*restoredParent,*restored,*scene,true);fixture.Select(restored);Check(DeletionNodeIndex(nodes->model(),restored->GetInstanceID()).isValid(),"Restored node missing from Hierarchy");
    auto* inspected=fixture.InspectorDock->findChild<QLineEdit*>("objectInstanceIDEdit");Check(inspected&&inspected->text().toULongLong()==restored->GetInstanceID(),"Inspector could not bind the recreated node");
    fixture.Pump([&]{return fixture.Viewport->GetStatistics().VisibleObjects>0;});
    Check(scenes->Undo(error)&&scene->GetMainCamera()==cameraA,"Undo SetMainCamera after Undo Delete failed to resolve Camera A");
    Check(scenes->Redo(error)&&scene->GetMainCamera()==restored->GetComponent<GameSproutRuntime::ICameraComponent>(),"Redo SetMainCamera retained Camera B's destroyed Runtime ID");
    Check(scenes->Redo(error),error);fixture.Settle();Check(!scene->FindChild("Delete subtree")&&!scene->GetMainCamera(),"Redo after camera history did not delete Camera B again");
    Check(scenes->Undo(error),error);fixture.Settle();restoredParent=scene->FindChild("Delete subtree");restored=restoredParent->FindChild("All components");fixture.Select(restored);

    // Only the focused tree/viewport owns node Delete; text widgets keep editing text.
    auto* search=hierarchyDock->findChild<QLineEdit*>("searchEdit");Check(search,"Hierarchy search missing");search->setText("ABCD");search->setCursorPosition(1);search->setFocus();QTest::keyClick(search,Qt::Key_Delete);
    Check(search->text()=="ACD"&&runtime->QueryGameObject(restored->GetInstanceID()),"Delete in Hierarchy search removed a scene node");search->clear();fixture.Settle();
    auto* name=fixture.InspectorDock->findChild<QLineEdit*>("objectNameEdit");Check(name&&!name->isReadOnly(),"Inspector node name missing");const QString oldName=name->text();name->setText("ABCD");name->setCursorPosition(1);name->setFocus();QTest::keyClick(name,Qt::Key_Delete);
    Check(name->text()=="ACD"&&runtime->QueryGameObject(restored->GetInstanceID()),"Delete in Inspector text removed a scene node");name->setText(oldName);fixture.Viewport->setFocus();fixture.Settle();
    const auto rootID=scene->GetInstanceID();fixture.Select(scene);fixture.Key(Qt::Key_Delete);Check(runtime->QueryGameObject(rootID)==scene,"Viewport deleted the scene root");
    fixture.Select(restoredParent);nodes->setFocus();QTest::keyClick(nodes,Qt::Key_Backspace);fixture.Settle();Check(!scene->FindChild("Delete subtree"),"Focused Hierarchy Backspace did not delete the subtree");
    Check(scenes->Undo(error),error);fixture.Settle();restoredParent=scene->FindChild("Delete subtree");restored=restoredParent->FindChild("All components");

    // A held key cannot consume the following sibling, even if selection moves there.
    auto* first=fixture.Mesh("Repeat first",{-2,0,0});auto* second=fixture.Mesh("Repeat second",{2,0,0});const auto firstID=first->GetInstanceID(),secondID=second->GetInstanceID();
    fixture.Select(first);nodes->setFocus();QTest::keyClick(nodes,Qt::Key_Delete);fixture.Settle();Check(!runtime->QueryGameObject(firstID),"Focused Hierarchy Delete did not remove its object");
    fixture.Select(second);nodes->setFocus();QKeyEvent repeated(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier,QString(),true,2);QCoreApplication::sendEvent(nodes,&repeated);fixture.Settle();Check(runtime->QueryGameObject(secondID),"Auto-repeat Delete removed another selected object");
    fixture.Viewport->setFocus();QCoreApplication::sendEvent(fixture.Viewport,&repeated);fixture.Settle();Check(runtime->QueryGameObject(secondID),"Viewport auto-repeat Delete removed an object");
    fixture.Key(Qt::Key_Backspace);Check(!runtime->QueryGameObject(secondID),"Focused viewport Backspace did not delete an object");

    // Delete during an active gizmo is rejected without cancelling its transaction.
    auto* protectedObject=fixture.Mesh("Protected drag",{0,0,0});fixture.Select(protectedObject);fixture.Key(Qt::Key_W);const auto protectedID=protectedObject->GetInstanceID();
    SceneGizmoTarget target;const auto transform=ReadTransform(*protectedObject);target.Local={transform.Position,transform.Rotation,transform.Scale};SceneGizmoSettings settings;const auto frame=fixture.Frame();settings.SizePixels*=float(frame.Height)/fixture.Viewport->height();settings.HitRadiusPixels*=float(frame.Height)/fixture.Viewport->height();
    SceneGizmo gizmo;Check(gizmo.Update(target,frame,settings,error),error);GameSproutRuntime::Vector2 physical;Check(SceneGizmo::Project(frame,gizmo.GetPivotWorld()+gizmo.GetAxis(0)*gizmo.GetHandleWorldSize()*.75f,physical),"Cannot project deletion guard handle");
    const QPoint point=fixture.Logical(physical).toPoint();auto* window=fixture.Viewport->windowHandle();QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);Check(scenes->HasTransformEdit(),"Deletion guard did not begin a real gizmo drag");
    QTest::keyClick(window,Qt::Key_Delete);Check(scenes->HasTransformEdit()&&runtime->QueryGameObject(protectedID),"Delete cancelled an active gizmo or removed its object");QTest::keyClick(window,Qt::Key_Escape);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point);fixture.Settle();
    fixture.Key(Qt::Key_Delete);Check(!runtime->QueryGameObject(protectedID),"Focused viewport Delete did not remove an idle selected node");
    Check(scenes->SaveScene(error),error);const auto saved=fixture.Services.ReadScene("ViewDeletion.scene");Check(scenes->OpenScene("Scenes/ViewDeletion.scene",error),error);fixture.Settle();
    Check(!scenes->GetActiveScene()->FindChild("Protected drag")&&!scenes->GetActiveScene()->FindChild("Repeat first")&&!scenes->GetActiveScene()->FindChild("Repeat second"),"Saved view deletions reappeared after reopen");
    Check(scenes->SaveScene(error)&&fixture.Services.ReadScene("ViewDeletion.scene")==saved,error);
    std::cout<<"PASS real Hierarchy menu/keyboard, viewport Delete, selection/Inspector/render cleanup, text/repeat/gizmo guards\n";
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);GRHI::GRHIApi* rhi=nullptr;
    try {
        if(QCoreApplication::arguments().contains("--module-only")){CheckModuleDeletion();return 0;}
        if(SceneViewport::IsHeadless()){std::cout<<"SKIP native deletion requires a desktop platform\n";return 0;}
        rhi=ExportGRHIApi(TEST_RUNTIME_ROOT);Check(rhi,"Cannot load GRHI for deletion view checks");
        {SceneInteractionFixture fixture(rhi);fixture.Initialize();CheckViewsDeletion(fixture);}
        delete rhi;return 0;
    }catch(const std::exception& exception){delete rhi;std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}

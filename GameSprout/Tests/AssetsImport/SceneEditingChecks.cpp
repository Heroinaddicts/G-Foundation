#include "ProjectViewTestSupport.h"
#include "ISceneManager.h"
#include "IRuntimeHost.h"
#include "IAnimationPreview.h"
#include "ISceneEditorUI.h"
#include "IHierarchyUI.h"
#include "ISelectionManager.h"
#include "../../View/Shared/AssetDragPayload.h"
#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QTreeWidget>
#include <QPainter>
#include <QDrag>
#include <QMouseEvent>

static QAction* Action(QMenu* menu,const QString& text) {
    if(menu)for(QAction* action:menu->actions())if(action->text()==text)return action;
    return nullptr;
}
static IAssetMeta* MetaAt(IAssetsManager* assets,const std::string& path) {
    for(IAssetMeta* meta:assets->GetAssetMetas())if(meta->GetPath()==path)return meta;
    return nullptr;
}
static GameSproutRuntime::ResourceReference ResourceAt(ILibraryManager* library,IAssetsManager* assets,
    const std::string& path,eAssetType type,std::size_t index=0) {
    IAssetMeta* meta=MetaAt(assets,path);Check(meta,"Resource source missing: "+path);
    AssetImportManifest manifest;Check(library->GetManifest(meta->_Guid,manifest),"Manifest missing: "+path);
    for(const auto& resource:manifest.Assets)if(resource.Type==type) {
        if(index--==0)return {manifest.GuidString,resource.LocalID};
    }
    throw std::runtime_error("Resource type missing: "+path);
}
static Json::Value ReadScene(const QString& path) {
    QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read saved scene");
    Json::Value document;Check(AssetArtifactIO::Parse(file.readAll().toStdString(),document),"Saved scene is not JSON");
    return document;
}
static void CommitLine(QWidget* owner,const char* name,const QString& text) {
    QLineEdit* field=owner?owner->findChild<QLineEdit*>(name):nullptr;
    Check(field&&!field->isReadOnly(),std::string("Editable field missing: ")+name);
    field->setText(text);
    Check(QMetaObject::invokeMethod(field,"editingFinished",Qt::DirectConnection),"Cannot finish field editing");
}
static QWidget* ResourceWidget(QWidget* owner,const char* hostName) {
    QWidget* host=owner?owner->findChild<QWidget*>(hostName):nullptr;
    QWidget* field=host?host->findChild<QWidget*>("ResourceFieldForm"):nullptr;
    Check(field,std::string("Resource field missing: ")+hostName);return field;
}
static bool DropResource(QWidget* field,const ProjectAssetSelection& value) {
    QMimeData* mime=AssetDragPayload::Encode({value});Check(mime,"Cannot encode drag identity");
    QDragEnterEvent enter(QPoint(5,5),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(field,&enter);
    bool accepted=false;
    if(enter.isAccepted()) {
        QDropEvent drop(QPointF(5,5),Qt::CopyAction,mime,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(field,&drop);accepted=drop.isAccepted();
    }
    delete mime;return accepted;
}
static QDialog* Picker() {
    for(QWidget* window:QApplication::topLevelWidgets())
        if(window->objectName()=="ResourcePickerDialog"&&window->isVisible())return qobject_cast<QDialog*>(window);
    return nullptr;
}
static void PickResource(const GameSproutRuntime::ResourceReference& reference) {
    QDialog* dialog=Picker();Check(dialog,"Resource picker not open");
    QTreeWidget* tree=dialog->findChild<QTreeWidget*>("resourceTree");Check(tree,"Resource list missing");
    for(int i=0;i<tree->topLevelItemCount();++i) {
        auto* item=tree->topLevelItem(i);
        if(item->data(0,Qt::UserRole).toString().toStdString()==reference.Guid&&
            item->data(0,Qt::UserRole+1).toULongLong()==reference.LocalID) {
            tree->setCurrentItem(item);
            dialog->findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Ok)->click();
            return;
        }
    }
    throw std::runtime_error("Expected resource missing from picker");
}

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    PreviewEditor editor(Export(TEST_BINARY_ROOT));
    Foundation::FoundationApi* foundation=nullptr;
    GameSproutEditor::GameSproutModuleApi* api=nullptr;
    IIDEWindow* ide=nullptr;
    std::vector<GameSproutEditor::IGameSproutView*> views;
    bool modulesReady=false;
    TestProject project;
    TestConsole console;
    QTemporaryDir temporary;
    const auto cleanup=[&] {
        for(auto it=views.rbegin();it!=views.rend();++it)GameSproutEditor::GameSproutEditorFinal::Release(*it,&editor);
        views.clear();
        if(modulesReady)for(const std::string& name:{"SelectionManager","AnimationPreview","SceneManager","RuntimeHost","AssetsManager","AssetsPipeline","LibraryManager"})
            if(editor.Modules.count(name))GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules[name],api);
        if(ide){GameSproutEditor::GameSproutEditorFinal::Release(ide,&editor);ide=nullptr;}
        if(foundation){foundation->Release();foundation=nullptr;}
        delete api;api=nullptr;
    };
    try {
        Check(temporary.isValid(),"Cannot create editor project");
        QDir root(temporary.path());Check(root.mkpath("Assets/Scenes"),"Cannot create Assets");
        QImage image(16,16,QImage::Format_RGBA8888);image.fill(QColor("#62a6e5"));
        Check(image.save(root.filePath("Assets/Color.png")),"Cannot write texture");
        std::string error;
        Check(AssetArtifactIO::Write(root.filePath("Assets/Default.mat"),"{\"Version\":1,\"Name\":\"Default\"}",error),error);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Imported.obj"),
            "o First\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\no Second\nv 2 0 0\nv 3 0 0\nv 2 1 0\nf 4 5 6\n",error),error);
        project.Root=temporary.path().toStdString();
        editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        ide=static_cast<IIDEWindow*>(LoadView("IDEWindow"));editor.IDE=ide;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(ide,&editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(ide,&editor),"IDE init failed");
        foundation=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(foundation&&foundation->Initialize(argc,argv)&&foundation->Launch(),"Foundation init failed");
        api=new GameSproutEditor::GameSproutModuleApi(foundation,&editor);
        for(const std::string& name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","RuntimeHost","AnimationPreview","SelectionManager","SceneManager"})
            editor.Modules[name]=LoadModule(name);
        for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,api);
        for(const auto& entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,api),"Module init failed: "+entry.first);
        modulesReady=true;
        for(const auto& entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,api),"Module launch failed: "+entry.first);
        for(const std::string& name:{"HierarchyUI","InspectorUI","ProjectAssetsUI","SceneEditorUI"}) {
            auto* view=LoadView(name);editor.Views[name]=view;views.push_back(view);
        }
        for(auto* view:views)GameSproutEditor::GameSproutEditorFinal::Before(view,&editor);
        for(auto* view:views)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,&editor),"View init failed");
        for(auto* view:views)Check(GameSproutEditor::GameSproutEditorFinal::Launch(view,&editor),"View launch failed");
        ide->Show();QCoreApplication::processEvents();
        auto* assets=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        auto* scenes=static_cast<ISceneManager*>(editor.Modules["SceneManager"]);
        auto* selection=static_cast<ISelectionManager*>(editor.Modules["SelectionManager"]);
        auto* sceneUI=static_cast<ISceneEditorUI*>(editor.Views["SceneEditorUI"]);
        const auto tick=[&] {
            foundation->Update();QCoreApplication::processEvents();
            for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Update(entry.second,api);
            for(auto* view:views)GameSproutEditor::GameSproutEditorFinal::Update(view,&editor);
        };
        const auto pump=[&](const std::function<bool()>& done) {
            const auto start=std::chrono::steady_clock::now();
            do{tick();if(done())return;QThread::msleep(2);}while(std::chrono::steady_clock::now()-start<std::chrono::seconds(30));
            throw std::runtime_error("Scene editing timed out");
        };
        GameSproutEditor::GameSproutEditorFinal::Finished(assets,api);
        pump([&]{
            if(assets->GetAssetMetas().size()<4)return false;
            for(IAssetMeta* meta:assets->GetAssetMetas())if(meta->GetImportState()==eAssetImportState::Pending)return false;
            return true;
        });
        const auto texture=ResourceAt(library,assets,"Color.png",eAssetType::Texture);
        const auto mesh=ResourceAt(library,assets,"Imported.obj",eAssetType::Mesh);
        const auto material=ResourceAt(library,assets,"Default.mat",eAssetType::Material);
        auto* scene=scenes->GetActiveScene();Check(scene,"No active scene");
        auto* object=scene->CreateGameObject("Editable");
        object->AddComponent<GameSproutRuntime::IMeshComponent>();
        object->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
        object->AddComponent<GameSproutRuntime::IImageComponent>();
        object->AddComponent<GameSproutRuntime::IRectTransformComponent>();
        object->AddComponent<GameSproutRuntime::ICanvasComponent>();
        auto* camera=object->AddComponent<GameSproutRuntime::ICameraComponent>();
        Check(camera&&scene->SetMainCamera(camera),"Cannot create editable scene");
        const auto objectID=object->GetInstanceID();
        Check(scenes->EditObject(objectID,"Rename",
            [](GameSproutRuntime::IGameObject& target,std::string&){target.SetName("Saved Bear");return target.GetName()=="Saved Bear";},
            [](GameSproutRuntime::IGameObject& target,std::string&){target.SetName("Editable");return target.GetName()=="Editable";},error),error);
        Check(scenes->CanUndo()&&scenes->Undo(error)&&object->GetName()=="Editable"&&scenes->CanRedo(),"Undo failed");
        Check(scenes->Redo(error)&&object->GetName()=="Saved Bear","Redo failed");
        object->GetTransform()->SetLocalPosition({1.25f,2.5f,-3});
        object->GetComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(mesh);
        object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({material,{}});
        object->GetComponent<GameSproutRuntime::IImageComponent>()->SetTexture(texture);
        object->GetComponent<GameSproutRuntime::IImageComponent>()->SetColor({0.25f,0.5f,0.75f,0.8f});
        object->GetComponent<GameSproutRuntime::IRectTransformComponent>()->SetSizeDelta({320,180});
        Check(object->GetComponent<GameSproutRuntime::ICanvasComponent>()->SetReferenceResolution({1280,720}),"Canvas settings rejected");
        auto settings=camera->GetSettings();settings.NearPlane=0.25f;Check(camera->SetSettings(settings),"Camera settings rejected");
        // Save before Runtime has dispatched any of the latest setters.
        Check(scenes->HasUnsavedChanges()&&scenes->SaveSceneAs("Scenes/Editing.scene",error),error);
        Check(!scenes->HasUnsavedChanges(),"Save left scene modified");
        const Json::Value first=ReadScene(root.filePath("Assets/Scenes/Editing.scene"));
        Check(first["Objects"].isArray(),"Saved document missing objects");
        Check(scenes->OpenScene("Scenes/Editing.scene",error),error);tick();
        scene=scenes->GetActiveScene();object=scene->FindChild("Saved Bear");
        Check(object&&object->GetInstanceID()!=objectID,"Reload retained ephemeral runtime ID");
        Check(object->GetTransform()->GetLocalPosition().X==1.25f&&scene->GetMainCamera()==object->GetComponent<GameSproutRuntime::ICameraComponent>(),"Reload lost transform/main camera");
        Check(object->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==mesh&&
            object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture()==texture&&
            object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterialCount()==2&&
            object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterial(0)==material,"Reload lost resource identities");
        Check(scenes->SaveScene(error),error);
        Check(ReadScene(root.filePath("Assets/Scenes/Editing.scene"))==first,"File IDs changed after reload/save");
        // 两次原子保存可能合并为迟到的 watcher 通知；等最终文件版本完成校验再操作 UI。
        pump([&] {
            IAssetMeta* savedScene=MetaAt(assets,"Scenes/Editing.scene");
            auto* pipeline=static_cast<IAssetsPipeline*>(editor.Modules["AssetsPipeline"]);
            if(!savedScene || savedScene->GetImportState()!=eAssetImportState::Ready || !pipeline->CanReuseAsset(savedScene))return false;
            for(IAssetMeta* meta:assets->GetAssetMetas())
                if(meta->GetImportState()==eAssetImportState::Pending)return false;
            for(QWidget* widget:QApplication::allWidgets())
                if(widget->objectName()=="loadingOverlay" && widget->isVisible())return false;
            return true;
        });
        selection->SetSceneObjectSelection({scene->GetInstanceID(),object->GetInstanceID()});tick();tick();
        QWidget* inspectorWindow=nullptr;
        QTreeView* hierarchyTree=nullptr;
        for(QWidget* window:QApplication::topLevelWidgets())
            if(auto* dock=window->findChild<QDockWidget*>("Inspector")){inspectorWindow=dock;break;}
        for(QWidget* window:QApplication::topLevelWidgets())
            if(auto* dock=window->findChild<QDockWidget*>("Hierarchy"))hierarchyTree=dock->findChild<QTreeView*>("hierarchyTree");
        Check(inspectorWindow,"Inspector dock not found");
        CommitLine(inspectorWindow,"objectNameEdit","Renamed In Inspector");tick();
        Check(object->GetName()=="Renamed In Inspector"&&scenes->HasUnsavedChanges(),"Inspector name did not write Runtime");
        Check(hierarchyTree&&hierarchyTree->model()->index(0,0).data().toString().endsWith(" *"),"Unsaved scene marker missing");
        CommitLine(inspectorWindow,"transformPositionXEdit","7.5");tick();
        Check(object->GetTransform()->GetLocalPosition().X==7.5f,"Transform field did not write Runtime");
        CommitLine(inspectorWindow,"rectTransformSizeDeltaXEdit","640");tick();
        CommitLine(inspectorWindow,"canvasResolutionXEdit","1600");tick();
        CommitLine(inspectorWindow,"imageColorREdit","0.6");tick();
        CommitLine(inspectorWindow,"cameraFovEdit","75");tick();
        Check(object->GetComponent<GameSproutRuntime::IRectTransformComponent>()->GetSizeDelta().X==640&&
            object->GetComponent<GameSproutRuntime::ICanvasComponent>()->GetReferenceResolution().X==1600&&
            std::fabs(object->GetComponent<GameSproutRuntime::IImageComponent>()->GetColor().R-0.6f)<0.0001f&&
            std::fabs(GameSproutRuntime::RadiansToDegrees(object->GetComponent<GameSproutRuntime::ICameraComponent>()->GetSettings().VerticalFovRadians)-75)<0.001f,
            "Component fields did not update");
        const float validNear=object->GetComponent<GameSproutRuntime::ICameraComponent>()->GetSettings().NearPlane;
        CommitLine(inspectorWindow,"cameraNearEdit","2000");tick();
        Check(object->GetComponent<GameSproutRuntime::ICameraComponent>()->GetSettings().NearPlane==validNear&&
            inspectorWindow->findChild<QLineEdit*>("cameraNearEdit")->text().toFloat()==validNear,"Invalid camera setting was not restored");
        auto* textureField=ResourceWidget(inspectorWindow,"textureResourceHost");
        textureField->findChild<QAction*>("clearResourceAction")->trigger();tick();
        Check(object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture().Guid.empty(),"Clear did not remove texture reference");
        Check(scenes->Undo(error)&&object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture()==texture,"Clear Undo failed");tick();
        textureField->findChild<QAction*>("clearResourceAction")->trigger();tick();
        Check(DropResource(textureField,{texture.Guid,0,false}),"Source texture drop rejected");tick();
        Check(object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture()==texture,"Source GUID+0 did not resolve main texture ID");
        auto* meshField=ResourceWidget(inspectorWindow,"meshResourceHost");
        const auto originalMesh=object->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh();
        Check(!DropResource(meshField,{texture.Guid,0,false})&&
            object->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==originalMesh,"Wrong-type drop modified mesh");
        Check(DropResource(meshField,{mesh.Guid,0,false}),"Model source drop rejected");
        QCoreApplication::processEvents();
        Check(Picker(),"Multi-mesh source did not open picker");
        const auto meshSecond=ResourceAt(library,assets,"Imported.obj",eAssetType::Mesh,1);
        PickResource(meshSecond);tick();
        Check(object->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==meshSecond,"Picker lost chosen mesh subresource ID");
        meshField->findChild<QToolButton*>("selectResourceButton")->click();QCoreApplication::processEvents();
        Check(Picker(),"Mesh selector did not open");
        // Context change must invalidate a pending picker rather than assigning into the new target.
        const auto editedObjectID=object->GetInstanceID();
        const auto sceneID=scene->GetInstanceID();
        selection->SetSceneObjectSelection({sceneID,sceneID});tick();tick();
        Check(!Picker(),"Picker survived target change");
        selection->SetSceneObjectSelection({sceneID,editedObjectID});tick();tick();
        auto* renderer=object->GetComponent<GameSproutRuntime::IMeshRendererComponent>();
        QWidget* slot=inspectorWindow->findChild<QWidget*>("materialSlot.1");Check(slot,"Material slot editor missing");
        slot->findChild<QToolButton*>("removeMaterialButton")->click();tick();
        Check(renderer->GetMaterialCount()==1,"Remove material slot did not shrink array");
        Check(scenes->Undo(error)&&renderer->GetMaterialCount()==2,"Material array Undo failed");tick();
        slot=inspectorWindow->findChild<QWidget*>("materialSlot.1");
        auto* materialField=ResourceWidget(slot,"materialResourceHost");
        Check(DropResource(materialField,{material.Guid,0,false}),"Material source drop rejected");tick();
        Check(renderer->GetMaterial(1)==material,"Material field did not bind actual main LocalID: got "
            +renderer->GetMaterial(1).Guid+"_"+std::to_string(renderer->GetMaterial(1).LocalID)
            +" expected "+material.Guid+"_"+std::to_string(material.LocalID));
        // Verify press-before-drag preserves the scene target during the gesture.
        auto* projectView=static_cast<IProjectAssetsUI*>(editor.Views["ProjectAssetsUI"]);
        Check(projectView->NavigateToDirectory(""),"Cannot navigate source assets");tick();tick();
        selection->SetSceneObjectSelection({sceneID,editedObjectID});tick();tick();
        QTreeView* assetTree=nullptr;
        for(QWidget* window:QApplication::topLevelWidgets())if(auto* dock=window->findChild<QDockWidget*>("ProjectAssets"))
            assetTree=dock->findChild<QTreeView*>("assetTree");
        Check(assetTree,"Project asset tree missing");
        QModelIndex textureRow=FindRow(assetTree,"Color.png");Check(textureRow.isValid(),"Drag source row missing");
        assetTree->scrollTo(textureRow);QCoreApplication::processEvents();
        const QPoint start=assetTree->visualRect(textureRow).center();
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(start),QPointF(assetTree->viewport()->mapToGlobal(start)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(assetTree->viewport(),&press);
        Check(selection->GetSceneObjectSelection().ObjectID==editedObjectID,"Project mouse press stole Inspector target before drag");
        bool dispatchedDrop=false;
        QTimer::singleShot(0,[&]{
            dispatchedDrop=DropResource(ResourceWidget(inspectorWindow,"textureResourceHost"),{texture.Guid,0,false});
            QDrag::cancel();
        });
        const QPoint moved=start+QPoint(QApplication::startDragDistance()+20,0);
        QMouseEvent move(QEvent::MouseMove,QPointF(moved),QPointF(assetTree->viewport()->mapToGlobal(moved)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(assetTree->viewport(),&move);
        QCoreApplication::processEvents();
        Check(dispatchedDrop,"Drag did not dispatch an accepted resource drop");
        Check(selection->GetSceneObjectSelection().ObjectID==editedObjectID,"Drag switched Inspector away from target");
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(moved),QPointF(assetTree->viewport()->mapToGlobal(moved)),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(assetTree->viewport(),&release);tick();
        Check(selection->GetSceneObjectSelection().ObjectID==editedObjectID,"Drag release committed asset selection");
        // Public Save command must commit to the currently opened .scene.
        sceneUI->SaveScene();tick();
        Check(!scenes->HasUnsavedChanges(),"File/Save Scene did not save");
        Check(!hierarchyTree->model()->index(0,0).data().toString().endsWith(" *"),"Saved scene retained modified marker");
        Check(scenes->OpenScene("Scenes/Editing.scene",error),"Cannot reopen edited document");
        object=scenes->GetActiveScene()->FindChild("Renamed In Inspector");
        Check(object&&object->GetTransform()->GetLocalPosition().X==7.5f&&
            object->GetComponent<GameSproutRuntime::IMeshComponent>()->GetMesh()==meshSecond&&
            object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture()==texture&&
            object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterial(1)==material,"Inspector edits/references were not saved");
        tick();
        const auto sceneBefore=scenes->GetActiveScene()->GetInstanceID();
        object->SetName("Unsaved Name");
        sceneUI->OpenSceneAsset("Scenes/Editing.scene");QCoreApplication::processEvents();
        auto* unsaved=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        Check(unsaved&&unsaved->objectName()=="UnsavedSceneDialog","Unsaved scene confirmation missing");
        unsaved->findChild<QPushButton*>("cancelButton")->click();tick();
        Check(scenes->GetActiveScene()->GetInstanceID()==sceneBefore&&object->GetName()=="Unsaved Name","Cancel lost unsaved state");
        sceneUI->OpenSceneAsset("Scenes/Editing.scene");QCoreApplication::processEvents();
        unsaved=qobject_cast<QDialog*>(QApplication::activeModalWidget());Check(unsaved,"Unsaved dialog did not reopen after Cancel");
        unsaved->findChild<QPushButton*>("discardButton")->click();tick();
        object=scenes->GetActiveScene()->FindChild("Renamed In Inspector");
        Check(object&&scenes->GetActiveScene()->GetInstanceID()!=sceneBefore,"Discard did not open saved scene");
        sceneUI->SaveSceneAs();QCoreApplication::processEvents();
        auto* save=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        Check(save&&save->objectName()=="SaveSceneDialog","Save As dialog missing");
        save->findChild<QLineEdit*>("pathEdit")->setText("../Escape.scene");
        save->findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Save)->click();
        Check(save->isVisible()&&!save->findChild<QLabel*>("errorLabel")->text().isEmpty(),"Save As escaped Assets or closed on failure");
        save->findChild<QLineEdit*>("pathEdit")->setText("Scenes/EditingCopy.scene");
        save->findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Save)->click();tick();
        Check(scenes->GetActiveScenePath()=="Scenes/EditingCopy.scene"&&QFileInfo::exists(root.filePath("Assets/Scenes/EditingCopy.scene")),"Save As did not publish source scene");
        selection->SetSceneObjectSelection({scenes->GetActiveScene()->GetInstanceID(),object->GetInstanceID()});tick();tick();
        QWidget* inspectorDock=inspectorWindow;
        inspectorDock->resize(680,1100);QCoreApplication::processEvents();
        Check(inspectorDock->grab().save("/tmp/gamesprout-scene-editing-preview.png"),"Cannot render editing UI preview");
        std::cout<<"PASS live Inspector edits, imported resource references, Undo/Redo and stable scene save/reopen\n";
        cleanup();return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';cleanup();return 1;}
}

#include "ProjectViewTestSupport.h"
#include "ISceneManager.h"
#include "IHierarchyUI.h"
#include <QAction>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSortFilterProxyModel>

static QAction* MenuAction(QMenu* menu,const QString& text) {
    if(menu) for(QAction* action:menu->actions()) if(action->text()==text) return action;
    throw std::runtime_error("Missing menu action: "+text.toStdString());
}
static QModelIndex NodeIndex(QAbstractItemModel* model,std::uint64_t id,const QModelIndex& parent={}) {
    for(int row=0;row<model->rowCount(parent);++row) {
        const QModelIndex index=model->index(row,0,parent);
        if(index.data(Qt::UserRole+1).toULongLong()==id) return index;
        const QModelIndex child=NodeIndex(model,id,index);
        if(child.isValid()) return child;
    }
    return {};
}
static void RememberRightClick(QTreeView* tree,const QModelIndex& index) {
    const QPoint local=index.isValid()?tree->visualRect(index).center():QPoint(tree->viewport()->width()-3,tree->viewport()->height()-3);
    const QPoint global=tree->viewport()->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(local),QPointF(global),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(),&press);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(local),QPointF(global),Qt::RightButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(tree->viewport(),&release);
}
static QByteArray ReadFile(const QString& path) {
    QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read scene fixture");return file.readAll();
}
class SceneOpenGuardProbe final : public GameSproutRuntime::IRuntimeListener {
public:
    ISceneManager* Scenes=nullptr;
    bool Called=false;
    bool Rejected=false;
    std::string Error;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {
        if(Called) return;
        Called=true;
        Rejected=!Scenes->OpenScene("Scenes/Created In Folder.scene",Error);
    }
};

#include "NodeInspectorChecks.h"
#include "AnimationInspectorChecks.h"

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    Foundation::FoundationApi* foundation=nullptr;
    IIDEWindow* ide=nullptr;
    IHierarchyUI* hierarchy=nullptr;
    IProjectAssetsUI* projectView=nullptr;
    IInspectorUI* inspector=nullptr;
    PreviewEditor editor;
    GameSproutEditor::GameSproutModuleApi* api=nullptr;
    IAssetsManager* assets=nullptr;
    ISceneManager* scenes=nullptr;
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create scene project");
        QDir root(temporary.path());Check(root.mkpath("Assets/Scenes")&&root.mkpath("Assets/Other"),"Cannot create scene directories");
        QImage sample(16,16,QImage::Format_RGBA8888);sample.fill(QColor(80,150,215));
        Check(sample.save(root.filePath("Assets/Sample.png")),"Cannot create node/asset selection texture");
        TestProject project;project.Root=temporary.path().toStdString();TestConsole console;
        editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        ide=static_cast<IIDEWindow*>(LoadView("IDEWindow"));editor.IDE=ide;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(ide,&editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(ide,&editor),"IDE startup failed");
        foundation=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(foundation&&foundation->Initialize(argc,argv)&&foundation->Launch(),"Foundation startup failed");
        api=new GameSproutEditor::GameSproutModuleApi(foundation,&editor);
        for(const std::string& name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","SelectionManager","SceneManager"}) editor.Modules[name]=LoadModule(name);
        for(const auto& entry:editor.Modules) GameSproutEditor::GameSproutEditorFinal::Before(entry.second,api);
        for(const auto& entry:editor.Modules) Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,api),"Module initialize failed: "+entry.first);
        for(const auto& entry:editor.Modules) if(entry.first!="Project") Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,api),"Module launch failed: "+entry.first);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project,api),"Project launch failed");
        scenes=static_cast<ISceneManager*>(editor.Modules["SceneManager"]);
        assets=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        hierarchy=static_cast<IHierarchyUI*>(LoadView("HierarchyUI"));projectView=LoadProjectView();
        inspector=static_cast<IInspectorUI*>(LoadView("InspectorUI"));
        for(GameSproutEditor::IGameSproutView* view:{static_cast<GameSproutEditor::IGameSproutView*>(hierarchy),static_cast<GameSproutEditor::IGameSproutView*>(projectView),static_cast<GameSproutEditor::IGameSproutView*>(inspector)}) {
            GameSproutEditor::GameSproutEditorFinal::Before(view,&editor);
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,&editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(view,&editor),"Scene View startup failed");
        }
        ide->Show();QCoreApplication::processEvents();
        auto* projectWindow=QApplication::activeWindow();
        Check(projectWindow!=nullptr,"IDE window missing");
        auto* hierarchyDock=projectWindow->findChild<QDockWidget*>("Hierarchy");
        auto* projectDock=projectWindow->findChild<QDockWidget*>("ProjectAssets");
        auto* inspectorDock=projectWindow->findChild<QDockWidget*>("Inspector");
        Check(hierarchyDock&&projectDock&&inspectorDock,"Scene dock missing");
        auto* hierarchyTree=hierarchyDock->findChild<QTreeView*>("hierarchyTree");
        auto* hierarchySearch=hierarchyDock->findChild<QLineEdit*>("searchEdit");
        auto* assetTree=projectDock->findChild<QTreeView*>("assetTree");
        Check(hierarchyTree&&hierarchySearch&&assetTree,"Scene controls missing");
        std::string stage="startup";
        auto tick=[&] {
            foundation->Update();QCoreApplication::processEvents();
            GameSproutEditor::GameSproutEditorFinal::Update(assets,api);
            GameSproutEditor::GameSproutEditorFinal::Update(editor.Modules["SelectionManager"],api);
            GameSproutEditor::GameSproutEditorFinal::Update(scenes,api);
            GameSproutEditor::GameSproutEditorFinal::Update(projectView,&editor);
            GameSproutEditor::GameSproutEditorFinal::Update(hierarchy,&editor);
            GameSproutEditor::GameSproutEditorFinal::Update(inspector,&editor);
        };
        auto pump=[&](const std::function<bool()>& done) {
            const auto start=std::chrono::steady_clock::now();
            do {tick();if(done())return;QThread::msleep(2);} while(std::chrono::steady_clock::now()-start<std::chrono::seconds(35));
            std::cerr<<"Scene timeout at "<<stage<<"\n";
            for(IAssetMeta* meta:assets->GetAssetMetas()) std::cerr<<meta->GetPath()<<" state="<<int(meta->GetImportState())<<" error="<<meta->GetImportError()<<'\n';
            for(int row=0;row<assetTree->model()->rowCount();++row)std::cerr<<"Visible "<<assetTree->model()->index(row,0).data().toString().toStdString()<<'\n';
            throw std::runtime_error("Scene operation timed out: "+stage);
        };
        auto idle=[&] {for(IAssetMeta* meta:assets->GetAssetMetas()) if(meta->GetImportState()==eAssetImportState::Pending)return false;return true;};
        GameSproutEditor::GameSproutEditorFinal::Finished(assets,api);
        pump([&]{return assets->GetAssetMetas().size()>=3&&idle();});
        stage="Hierarchy creation and scene Inspector";
        CheckNodeInspectorFlow(scenes,static_cast<ISelectionManager*>(editor.Modules["SelectionManager"]),hierarchy,
            projectView,inspector,hierarchyTree,assetTree,hierarchyDock,inspectorDock,root,tick,pump);
        auto* defaultScene=scenes->GetActiveScene();
        Check(defaultScene&&scenes->GetActiveScenePath().empty(),"Default unsaved scene missing");
        auto* mainCamera=defaultScene->GetMainCamera();
        Check(mainCamera&&mainCamera->GetGameObject()->GetName()=="Main Camera","Default main camera missing");
        auto* cameraObject=mainCamera->GetGameObject();
        auto cameraID=cameraObject->GetInstanceID();
        Check(NodeIndex(hierarchyTree->model(),cameraID).isValid(),"Hierarchy did not query default Runtime scene");
        hierarchyTree->setCurrentIndex(NodeIndex(hierarchyTree->model(),cameraID));
        Check(hierarchy->GetSelectedObjectID()==cameraID,"Hierarchy selection lost Runtime identity");
        cameraObject->SetName("Editor Camera");
        tick();
        if(NodeIndex(hierarchyTree->model(),cameraID).data().toString()!="Editor Camera"||hierarchy->GetSelectedObjectID()!=cameraID)
            std::cerr<<"Rename diagnostic: runtime="<<cameraObject->GetName()<<" tree="<<NodeIndex(hierarchyTree->model(),cameraID).data().toString().toStdString()<<" selected="<<hierarchy->GetSelectedObjectID()<<" expected="<<cameraID<<'\n';
        Check(NodeIndex(hierarchyTree->model(),cameraID).data().toString()=="Editor Camera"&&hierarchy->GetSelectedObjectID()==cameraID,"Runtime rename event lost selected node");
        auto* parent=defaultScene->CreateGameObject("Group");
        Check(parent->AddChild(cameraObject,false),"Cannot reparent camera");tick();
        const auto cameraIndex=NodeIndex(hierarchyTree->model(),cameraID);
        Check(cameraIndex.parent().data(Qt::UserRole+1).toULongLong()==parent->GetInstanceID(),"Hierarchy did not reflect Runtime reparent");
        parent->SetActive(false);tick();
        Check(!NodeIndex(hierarchyTree->model(),cameraID).data(Qt::UserRole+2).toBool(),"Inactive hierarchy state not reflected");
        parent->SetActive(true);
        hierarchySearch->setText("Camera");tick();
        Check(NodeIndex(hierarchyTree->model(),cameraID).isValid(),"Hierarchy search lost matching descendant");
        hierarchySearch->clear();tick();
        hierarchyDock->resize(420,700);QCoreApplication::processEvents();
        Check(hierarchyDock->grab().save("/tmp/gamesprout-hierarchy-preview.png"),"Hierarchy preview rendering failed");
        auto* inspectorLock=inspectorDock->findChild<QToolButton*>("lockButton");
        inspectorLock->setChecked(true);tick();

        // 右键文件夹决定创建目录。新建只写资产，不自动切换当前场景。
        Check(projectView->NavigateToDirectory(""),"Cannot navigate Project root");tick();
        assetTree->scrollTo(FindRow(assetTree,"Scenes"));QCoreApplication::processEvents();
        RememberRightClick(assetTree,FindRow(assetTree,"Scenes"));
        QMenu* context=projectDock->findChild<QMenu*>("ProjectAssets.ContentContextMenu");
        QAction* createScene=MenuAction(MenuAction(context,"Create")->menu(),"Scene");
        Check(createScene->isEnabled(),"Scene creation menu not registered");
        createScene->trigger();QCoreApplication::processEvents();
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
        Check(dialog&&dialog->objectName()=="CreateSceneDialog","Create Scene dialog did not open");
        auto* nameEdit=dialog->findChild<QLineEdit*>("sceneNameEdit");
        auto* errorLabel=dialog->findChild<QLabel*>("errorLabel");
        auto* location=dialog->findChild<QLabel*>("directoryLabel");
        auto* buttons=dialog->findChild<QDialogButtonBox*>("buttonBox");
        Check(nameEdit&&errorLabel&&location&&buttons&&location->text()=="Assets/Scenes","Scene dialog context directory wrong");
        nameEdit->setText("../escape");buttons->button(QDialogButtonBox::Ok)->click();
        Check(dialog->isVisible()&&!errorLabel->text().isEmpty(),"Invalid scene name did not keep dialog open");
        nameEdit->setText("Created In Folder");
        const auto originalSceneID=defaultScene->GetInstanceID();
        buttons->button(QDialogButtonBox::Ok)->click();QCoreApplication::processEvents();
        const QString createdFile=root.filePath("Assets/Scenes/Created In Folder.scene");
        Check(QFileInfo::exists(createdFile)&&scenes->GetActiveScene()->GetInstanceID()==originalSceneID,"Creation auto-opened scene or wrote wrong directory");
        Json::Value source;
        const QByteArray originalBytes=ReadFile(createdFile);
        Check(AssetArtifactIO::Parse(originalBytes.toStdString(),source)&&source["Version"].asUInt()==1&&source["MainCameraID"].asUInt64()==1,"New scene source document invalid");
        stage="created scene asset import/selection";
        pump([&]{return FindRow(assetTree,"Created In Folder.scene").isValid()&&idle();});
        auto selection=projectView->GetSelection();
        Check(selection.size()==1&&assets->QueryAsset(Guid(selection[0].GuidString))->GetPath()=="Scenes/Created In Folder.scene","Created scene did not become selected by asset identity");
        QModelIndex sceneRow=FindRow(assetTree,"Created In Folder.scene");
        Check(QMetaObject::invokeMethod(assetTree,"doubleClicked",Qt::DirectConnection,Q_ARG(QModelIndex,sceneRow)),"Cannot double-click Scene asset");
        tick();
        Check(scenes->GetActiveScenePath()=="Scenes/Created In Folder.scene"&&scenes->GetActiveScene()->GetName()=="Created In Folder", "Scene double-click did not activate source scene");
        Check(!scenes->GetRuntime()->QueryGameObject(originalSceneID)&&scenes->GetActiveScene()->GetMainCamera(),"Old scene retained or loaded main camera absent");
        Check(hierarchy->GetSelectedObjectID()==0&&NodeIndex(hierarchyTree->model(),scenes->GetActiveScene()->GetMainCamera()->GetGameObject()->GetInstanceID()).isValid(),"Hierarchy retained old selection after scene switch");
        Check(static_cast<ISelectionManager*>(editor.Modules["SelectionManager"])->GetSceneObjectSelection()==SceneObjectSelection{},"Scene switch retained invalid scene selection");
        QWidget* previousNodePanel=inspectorDock->findChild<QWidget*>("GameObjectInspectorForm");
        Check(!previousNodePanel||!previousNodePanel->isVisible()||inspectorDock->findChild<QLineEdit*>("localIDEdit")->text().toULongLong()!=cameraID,"Scene switch retained locked deleted node in Inspector");
        inspectorLock->setChecked(false);tick();

        // 同名创建失败不覆盖文件，窗口仍可取消。
        createScene->trigger();QCoreApplication::processEvents();
        dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());Check(dialog!=nullptr,"Second Scene dialog missing");
        nameEdit=dialog->findChild<QLineEdit*>("sceneNameEdit");buttons=dialog->findChild<QDialogButtonBox*>("buttonBox");
        nameEdit->setText("Created In Folder");buttons->button(QDialogButtonBox::Ok)->click();
        Check(dialog->isVisible()&&!dialog->findChild<QLabel*>("errorLabel")->text().isEmpty()&&ReadFile(createdFile)==originalBytes,"Collision overwrote source or closed dialog");
        buttons->button(QDialogButtonBox::Cancel)->click();QCoreApplication::processEvents();

        // 创建资源后导入尚未回调时，用户转而选节点；待导入完成不能把选择抢回去。
        createScene->trigger();QCoreApplication::processEvents();
        dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());Check(dialog,"Pending Scene dialog missing");
        dialog->findChild<QLineEdit*>("sceneNameEdit")->setText("Pending Choice");
        dialog->findChild<QDialogButtonBox*>("buttonBox")->button(QDialogButtonBox::Ok)->click();
        const auto pendingCameraID=scenes->GetActiveScene()->GetMainCamera()->GetGameObject()->GetInstanceID();
        const QModelIndex pendingNode=NodeIndex(hierarchyTree->model(),pendingCameraID);Check(pendingNode.isValid(),"Pending selection node missing");
        hierarchyTree->selectionModel()->setCurrentIndex(pendingNode,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        Check(static_cast<ISelectionManager*>(editor.Modules["SelectionManager"])->GetSceneObjectSelection().ObjectID==pendingCameraID,"Node selection did not override pending creation");
        stage="pending scene import preserves newer node selection";
        pump([&]{return FindRow(assetTree,"Pending Choice.scene").isValid()&&idle();});
        Check(static_cast<ISelectionManager*>(editor.Modules["SelectionManager"])->GetSceneObjectSelection().ObjectID==pendingCameraID&&hierarchy->GetSelectedObjectID()==pendingCameraID&&inspectorDock->findChild<QLineEdit*>("localIDEdit")->text().toULongLong()==pendingCameraID,"Pending Scene asset import stole newer node selection");

        std::string path,error;
        Check(!scenes->CreateSceneAsset("../","Escape",path,error)&&!error.empty(),"Parent traversal accepted");
        Check(!scenes->CreateSceneAsset(root.absolutePath().toStdString(),"Escape",path,error),"Absolute create directory accepted");
        Check(!scenes->CreateSceneAsset("Missing","Missing",path,error),"Missing create directory accepted");
        Check(!scenes->CreateSceneAsset("Scenes","CON",path,error),"Reserved cross-platform scene name accepted");
        Check(!scenes->CreateSceneAsset("Scenes","Created In Folder.SCENE",path,error)&&ReadFile(createdFile)==originalBytes,"Case-insensitive scene suffix bypassed collision protection");
        Check(scenes->CreateSceneAsset("Other","中文 场景",path,error)&&path=="Other/中文 场景.scene","UTF-8 scene creation returned wrong relative Assets path");
        Check(root.mkpath("External"),"Cannot create symlink boundary fixture");
        std::error_code symlinkError;
        std::filesystem::create_directory_symlink(std::filesystem::path(root.filePath("External").toStdString()),
            std::filesystem::path(root.filePath("Assets/Linked").toStdString()),symlinkError);
        if(!symlinkError)Check(!scenes->CreateSceneAsset("Linked","Escape",path,error),"Symlink create directory escaped Assets boundary");
        const auto activeID=scenes->GetActiveScene()->GetInstanceID();
        const auto count=scenes->GetRuntime()->GetSceneCount();
        bool attemptedInsideTraversal=false;
        scenes->GetActiveScene()->ForeachChild([&](GameSproutRuntime::IGameObject*) {
            attemptedInsideTraversal=true;
            Check(!scenes->OpenScene("Scenes/Created In Folder.scene",error)&&!error.empty(),"OpenScene allowed during Runtime traversal");
        });
        Check(attemptedInsideTraversal&&scenes->GetActiveScene()->GetInstanceID()==activeID&&scenes->GetRuntime()->GetSceneCount()==count,"Traversal open changed active scene");
        SceneOpenGuardProbe probe;probe.Scenes=scenes;
        Check(scenes->GetRuntime()->RegisterListener(&probe),"Cannot register scene reentry probe");
        scenes->GetActiveScene()->GetMainCamera()->GetGameObject()->SetName("Reentrant Camera");tick();
        scenes->GetRuntime()->UnregisterListener(&probe);
        Check(probe.Called&&probe.Rejected&&!probe.Error.empty()&&scenes->GetActiveScene()->GetInstanceID()==activeID,"OpenScene allowed inside Runtime event callback");
        Check(!scenes->OpenScene("../Outside.scene",error)&&scenes->GetActiveScene()->GetInstanceID()==activeID,"Outside open path replaced active scene");
        auto rejectDocument=[&](const Json::Value& bad,const char* message) {
            Check(AssetArtifactIO::Write(root.filePath("Assets/Bad.scene"),QByteArray::fromStdString(AssetArtifactIO::JsonText(bad)),error),error);
            Check(!scenes->OpenScene("Bad.scene",error)&&!error.empty()&&scenes->GetActiveScene()->GetInstanceID()==activeID&&scenes->GetRuntime()->GetSceneCount()==count,message);
        };
        Json::Value bad=source;bad["Version"]=2;rejectDocument(bad,"Unsupported version replaced scene");
        bad=source;bad["Objects"].append(source["Objects"][0]);rejectDocument(bad,"Duplicate scene IDs accepted");
        bad=source;bad["Objects"][0]["ParentID"]=Json::UInt64(1);rejectDocument(bad,"Parent cycle accepted");
        bad=source;bad["MainCameraID"]=Json::UInt64(999);rejectDocument(bad,"Missing main camera accepted");
        bad=source;bad["Objects"][0]["Components"][1]["Settings"]["NearPlane"]=-1;rejectDocument(bad,"Invalid camera settings replaced scene");
        bad=source;bad["Objects"][0]["Components"][1]["Type"]="Unknown.Component";rejectDocument(bad,"Unknown component replaced scene");
        Check(AssetArtifactIO::Write(root.filePath("Assets/Bad.scene"),"invalid json",error),error);
        Check(!scenes->OpenScene("Bad.scene",error)&&scenes->GetActiveScene()->GetInstanceID()==activeID,"Malformed JSON replaced scene");
        Check(scenes->GetActiveScene()->DestroyGameObject(scenes->GetActiveScene()->GetMainCamera()->GetGameObject()),"Cannot delete active main camera");tick();
        Check(!scenes->GetActiveScene()->GetMainCamera(),"Destroyed main camera left dangling reference");

        stage="Animator and SkinnedMeshRenderer Designer Inspector";
        CheckAnimationInspectorFlow(scenes,static_cast<ISelectionManager*>(editor.Modules["SelectionManager"]),
            inspectorDock,root,tick,pump);

        GameSproutEditor::GameSproutEditorFinal::Release(inspector,&editor);inspector=nullptr;
        GameSproutEditor::GameSproutEditorFinal::Release(projectView,&editor);projectView=nullptr;
        GameSproutEditor::GameSproutEditorFinal::Release(hierarchy,&editor);hierarchy=nullptr;
        GameSproutEditor::GameSproutEditorFinal::Release(scenes,api);scenes=nullptr;
        GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["SelectionManager"],api);
        GameSproutEditor::GameSproutEditorFinal::Release(assets,api);assets=nullptr;
        GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["AssetsPipeline"],api);
        GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules["LibraryManager"],api);
        GameSproutEditor::GameSproutEditorFinal::Release(ide,&editor);ide=nullptr;
        foundation->Release();foundation=nullptr;delete api;api=nullptr;
        std::cout<<"PASS real Runtime/Hierarchy creation menus, shared asset/node selection, component Inspector/lock/deletion, scene creation/open and invalid-document preservation\n";
        return 0;
    } catch(const std::exception& exception) {
        std::cerr<<"FAIL "<<exception.what()<<'\n';
        if(inspector)GameSproutEditor::GameSproutEditorFinal::Release(inspector,&editor);
        if(projectView)GameSproutEditor::GameSproutEditorFinal::Release(projectView,&editor);
        if(hierarchy)GameSproutEditor::GameSproutEditorFinal::Release(hierarchy,&editor);
        if(scenes)GameSproutEditor::GameSproutEditorFinal::Release(scenes,api);
        if(assets)GameSproutEditor::GameSproutEditorFinal::Release(assets,api);
        for(const std::string& name:{"AssetsPipeline","LibraryManager"})if(editor.Modules.count(name))GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules[name],api);
        if(ide)GameSproutEditor::GameSproutEditorFinal::Release(ide,&editor);
        if(foundation)foundation->Release();
        delete api;
        return 1;
    }
}

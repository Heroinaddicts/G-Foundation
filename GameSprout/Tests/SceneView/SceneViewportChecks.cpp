#include "SceneViewport.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include "IHierarchyUI.h"
#include "GameSproutRuntimeExport.h"
#include "IRuntimeHost.h"
#include "../Shared/ViewportRuntimeSpy.h"
#include "../AssetsImport/ProjectViewTestSupport.h"
#include <QDockWidget>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QScreen>
#include <QMenu>
#include <QAction>
#include <cstring>

class SceneFixture final : public ISceneManager {
public:
    GameSproutRuntime::IScene* Scene=nullptr;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene() const override{return Scene;}
    const std::string& GetActiveScenePath() const override{return Path;}
    bool OpenScene(const std::string&,std::string&) override{return false;}
    bool EditObject(uint64_t,const std::string&,const fSceneEditOperation&,const fSceneEditOperation&,std::string&) override{return false;}
    bool Undo(std::string&) override{return false;}
    bool Redo(std::string&) override{return false;}
    bool CanUndo() const override{return false;}
    bool CanRedo() const override{return false;}
    bool SaveScene(std::string&) override{return false;}
    bool SaveSceneAs(const std::string&,std::string&) override{return false;}
    bool HasUnsavedChanges() const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&) override{return false;}
    bool RegisterSceneListener(ISceneListener*) override{return true;}
    void UnregisterSceneListener(ISceneListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class SelectionFixture final : public ISelectionManager {
public:
    SceneObjectSelection Selected;
    std::vector<ProjectAssetSelection> GetAssetSelection() const override{return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>&) override{}
    SceneObjectSelection GetSceneObjectSelection() const override{return Selected;}
    void SetSceneObjectSelection(const SceneObjectSelection& value) override{Selected=value;}
    bool RegisterSelectionListener(ISelectionListener*) override{return true;}
    void UnregisterSelectionListener(ISelectionListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class HostFixture final : public IRuntimeHost {
public:
    bool Playing=false;
    void SetSimulationPlaying(bool value)override{Playing=value;}
    bool IsSimulationPlaying()const override{return Playing;}
    void ResetClock()override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class WindowEditor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit WindowEditor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    SceneFixture Scenes;
    HostFixture Host;
    SelectionFixture Selection;
    IIDEWindow* IDE=nullptr;
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        if(name=="RuntimeHost")return const_cast<HostFixture*>(&Host);
        if(name=="SceneManager")return const_cast<SceneFixture*>(&Scenes);
        if(name=="SelectionManager")return const_cast<SelectionFixture*>(&Selection);
        return nullptr;
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override {
        if(name=="IDEWindow")return IDE;
        const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;
    }
};
class TriangleResources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData& data,std::string& error) override {
        struct Vertex{float Position[3];float Normal[3];float Color[4];float UV[2];};
        const Vertex vertices[]={{{-1,-1,0},{0,0,1},{1,0.2f,0.1f,1},{0,0}},{{1,-1,0},{0,0,1},{0.1f,1,0.2f,1},{1,0}},{{0,1,0},{0,0,1},{0.2f,0.1f,1,1},{0.5f,1}}};
        const std::uint32_t indices[]={0,1,2};
        data={};data.VertexCount=3;data.VertexStride=sizeof(Vertex);data.IndexCount=3;data.IndexOffset=sizeof(vertices);data.ColorOffset=24;data.UVOffset=40;
        data.Bytes.resize(sizeof(vertices)+sizeof(indices));std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.Sections.push_back({0,3,0});data.LocalBounds.Extents={1,1,0};error.clear();return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData& data,std::string& error) override{data={};error="No texture required by this triangle";return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData& data,std::string& error) override{data={};data.DoubleSided=true;error.clear();return true;}
};
static void Mouse(QWidget* target,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
    QMouseEvent event(type,point,QPointF(target->mapToGlobal(point.toPoint())),button,buttons,modifiers);QCoreApplication::sendEvent(target,&event);
}
static bool Same(GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b){return (a-b).Length()<0.0001f;}
static std::size_t CountNodes(GameSproutRuntime::IGameObject* root) {
    std::size_t count=0;std::vector<GameSproutRuntime::IGameObject*> pending{root};
    while(!pending.empty()){auto* node=pending.back();pending.pop_back();++count;for(std::size_t index=0;index<node->GetChildCount();++index)pending.push_back(node->GetChild(index));}
    return count;
}
static QModelIndex NodeIndex(QAbstractItemModel* model,std::uint64_t id,const QModelIndex& parent={}) {
    for(int row=0;row<model->rowCount(parent);++row) {
        const QModelIndex index=model->index(row,0,parent);
        if(index.data(Qt::UserRole+1).toULongLong()==id)return index;
        const QModelIndex child=NodeIndex(model,id,index);if(child.isValid())return child;
    }
    return {};
}
static void DoubleClickNode(QTreeView* tree,std::uint64_t id) {
    tree->expandAll();const QModelIndex index=NodeIndex(tree->model(),id);Check(index.isValid(),"Hierarchy focus target missing");
    tree->scrollTo(index);QCoreApplication::processEvents();
    const QPointF point=tree->visualRect(index).center();
    Mouse(tree->viewport(),QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);
    Mouse(tree->viewport(),QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);
    Mouse(tree->viewport(),QEvent::MouseButtonDblClick,point,Qt::LeftButton,Qt::LeftButton);
    Mouse(tree->viewport(),QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);
    QCoreApplication::processEvents();
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    const bool headless=QCoreApplication::arguments().contains("--headless");
    if(!headless&&SceneViewport::IsHeadless()){std::cout<<"SKIP native viewport requires a real desktop platform\n";return 0;}
    ViewportRuntimeSpy spy;spy.RejectNativeCalls=true;
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    ISceneViewUI* view=nullptr;IIDEWindow* ide=nullptr;IHierarchyUI* hierarchy=nullptr;
    WindowEditor* editor=nullptr;
    TriangleResources resources;
    bool viewportDestroyed=false;
    try {
        if(!headless) {
            runtime=Export(TEST_RUNTIME_ROOT);Check(runtime,"Runtime SDK load failed");
        }
        editor=new WindowEditor(headless?&spy:runtime);
        GameSproutRuntime::IGameObject* mesh=nullptr;
        GameSproutRuntime::ICameraComponent* gameCamera=nullptr;
        GameSproutRuntime::Mat4 gameWorld;
        std::size_t nodeCount=0;
        if(runtime) {
            auto* scene=runtime->CreateScene("Native triangle");editor->Scenes.Scene=scene;
            mesh=scene->CreateGameObject("Triangle");
            mesh->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"11111111111111111111111111111111",1});
            mesh->AddComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterial(0,{"22222222222222222222222222222222",2});
            auto* cameraObject=scene->CreateGameObject("Game Camera");cameraObject->GetTransform()->SetLocalPosition({10,20,30});
            gameCamera=cameraObject->AddComponent<GameSproutRuntime::ICameraComponent>();Check(scene->SetMainCamera(gameCamera),"Cannot set Game camera");
            gameWorld=cameraObject->GetTransform()->GetWorldMatrix();nodeCount=CountNodes(scene);
            editor->Selection.Selected={scene->GetInstanceID(),mesh->GetInstanceID()};
        }
        ide=static_cast<IIDEWindow*>(LoadView("IDEWindow"));editor->IDE=ide;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(ide,editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(ide,editor),"IDE startup failed");
        view=static_cast<ISceneViewUI*>(LoadView("SceneViewUI"));
        editor->Views["SceneViewUI"]=view;
        GameSproutEditor::GameSproutEditorFinal::Before(view,editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(view,editor),"Scene view startup failed");
        hierarchy=static_cast<IHierarchyUI*>(LoadView("HierarchyUI"));editor->Views["HierarchyUI"]=hierarchy;
        GameSproutEditor::GameSproutEditorFinal::Before(hierarchy,editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(hierarchy,editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(hierarchy,editor),"Hierarchy startup failed");
        ide->Show();view->Show();QCoreApplication::processEvents();
        SceneViewport* viewport=nullptr;
        for(QWidget* window:QApplication::topLevelWidgets())if(auto* dock=window->findChild<QDockWidget*>("Scene")){viewport=dock->findChild<SceneViewport*>("sceneViewport");if(viewport)break;}
        Check(viewport,"Promoted native sceneViewport missing");
        if(runtime)viewport->Initialize(runtime,&resources);
        QObject::connect(viewport,&QObject::destroyed,&app,[&]{viewportDestroyed=true;});
        auto* host=viewport->window();host->resize(880,650);host->raise();host->activateWindow();QApplication::setActiveWindow(host);viewport->setFocus();QCoreApplication::processEvents();
        const auto tick=[&]{QCoreApplication::processEvents();if(runtime)runtime->Update(0);GameSproutEditor::GameSproutEditorFinal::Update(view,editor);GameSproutEditor::GameSproutEditorFinal::Update(hierarchy,editor);};
        const auto pump=[&](const std::function<bool()>& done){QElapsedTimer timer;timer.start();do{tick();if(done())return;QThread::msleep(5);}while(timer.elapsed()<15000);throw std::runtime_error("Native scene timed out: "+view->GetRenderError());};
        if(headless) {
            tick();Check(!view->IsRenderingAvailable()&&spy.NativeCalls==0&&view->GetRenderError().find("离屏")!=std::string::npos,"Headless passed a fake window to Runtime");
        }else {
            auto state=view->GetCameraState();state.Position={0,0,3};state.OrbitPivot={0,0,0};state.YawDegrees=0;state.PitchDegrees=0;state.OrbitDistance=3;
            Check(view->SetCameraState(state),"Cannot set Scene camera override");
            pump([&]{return view->IsRenderingAvailable()&&viewport->GetStatistics().Triangles>=1;});
            auto stats=viewport->GetStatistics();Check(stats.DrawCalls>0&&stats.VisibleObjects>0,"Native test only cleared and did not draw triangle");
            std::cout<<"Native first frame "<<stats.Width<<'x'<<stats.Height<<" logical="<<viewport->width()<<'x'<<viewport->height()<<" dpr="<<viewport->devicePixelRatioF()<<'\n';
            Check(std::abs(int(stats.Width)-qRound(viewport->width()*viewport->devicePixelRatioF()))<=2&&std::abs(int(stats.Height)-qRound(viewport->height()*viewport->devicePixelRatioF()))<=2,"Native SurfaceFrame did not match physical Retina pixels");
            const auto originalWidth=stats.Width;host->resize(1060,770);QCoreApplication::processEvents();
            ide->ResizeDockers({ide->QueryDocker("Scene")},{1},Qt::Horizontal);QCoreApplication::processEvents();
            std::cout<<"Native resized logical="<<viewport->width()<<'x'<<viewport->height()<<" dpr="<<viewport->devicePixelRatioF()<<std::endl;
            Check(qRound(viewport->width()*viewport->devicePixelRatioF())!=int(originalWidth),"Qt dock resize fixture did not change native viewport width");
            pump([&]{const auto current=viewport->GetStatistics();return int(current.Width)==qRound(viewport->width()*viewport->devicePixelRatioF())&&int(current.Height)==qRound(viewport->height()*viewport->devicePixelRatioF())&&current.Triangles>=1;});
            stats=viewport->GetStatistics();Check(std::abs(int(stats.Width)-qRound(viewport->width()*viewport->devicePixelRatioF()))<=2,"Foreign native window cached pre-resize pixel width");
            tick();QThread::msleep(50);QCoreApplication::processEvents();
            if(QScreen* screen=host->screen()) {
                const QPixmap capture=screen->grabWindow(host->winId());
                std::cout<<"Native window screenshot "<<(!capture.isNull()&&capture.save("/tmp/gamesprout-scene-native.png")?"saved":"unavailable")<<'\n';
            }
            // Native child reparenting must preserve the view object and recreate the
            // foreign Metal surface while its old NSView is still valid.
            auto* sceneDock=host->findChild<QDockWidget*>("Scene");Check(sceneDock,"Scene dock missing before native float regression");
            auto* sceneMenu=sceneDock->findChild<QMenu*>("DockerContextMenu.Scene");
            auto* floatAction=sceneMenu?sceneMenu->findChild<QAction*>("DockerFloat"):nullptr;
            Check(floatAction,"Scene float action missing");
            const auto beforeFloat=view->GetCameraState();
            floatAction->trigger();sceneDock->resize(630,420);sceneDock->show();sceneDock->raise();
            Check(sceneDock->isFloating()&&viewport->window()==sceneDock,"Scene did not become an independent native host");
            pump([&]{const auto current=viewport->GetStatistics();return view->IsRenderingAvailable()&&current.Triangles>=1&&int(current.Width)==qRound(viewport->width()*viewport->devicePixelRatioF())&&int(current.Height)==qRound(viewport->height()*viewport->devicePixelRatioF());});
            std::cout<<"Native floating Scene "<<viewport->GetStatistics().Width<<'x'<<viewport->GetStatistics().Height<<std::endl;
            ide->QueryDocker("Scene")->Layout(eDockerLayout::Right);ide->QueryDocker("Scene")->Activate();
            Check(!sceneDock->isFloating()&&viewport->window()==host,"Scene did not return to original native IDE host");
            pump([&]{const auto current=viewport->GetStatistics();return view->IsRenderingAvailable()&&current.Triangles>=1&&int(current.Width)==qRound(viewport->width()*viewport->devicePixelRatioF())&&int(current.Height)==qRound(viewport->height()*viewport->devicePixelRatioF());});
            Check(sceneDock->findChild<SceneViewport*>("sceneViewport")==viewport&&Same(beforeFloat.Position,view->GetCameraState().Position)&&Same(beforeFloat.OrbitPivot,view->GetCameraState().OrbitPivot),"Floating/re-docking recreated Scene contents or moved its camera");
            Check(CountNodes(editor->Scenes.Scene)==nodeCount&&editor->Scenes.Scene->GetMainCamera()==gameCamera,"Floating native Scene changed Game main camera or scene nodes");
            const auto afterFloat=gameCamera->GetGameObject()->GetTransform()->GetWorldMatrix();
            for(int index=0;index<16;++index)Check(afterFloat.Data[index]==gameWorld.Data[index],"Floating native Scene changed Game camera matrix");
            std::cout<<"Native re-docked Scene "<<viewport->GetStatistics().Width<<'x'<<viewport->GetStatistics().Height<<std::endl;
            host->raise();host->activateWindow();QApplication::setActiveWindow(host);viewport->setFocus();tick();
        }
        const auto original=view->GetCameraState();
        Mouse(viewport,QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton);
        Mouse(viewport,QEvent::MouseMove,{145,80},Qt::NoButton,Qt::RightButton);
        Check(view->GetCameraState().YawDegrees!=original.YawDegrees&&Same(view->GetCameraState().Position,original.Position),"RMB did not rotate independent camera in place");
        const auto beforeFlight=view->GetCameraState();
        QKeyEvent forward(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier);QCoreApplication::sendEvent(viewport,&forward);
        QThread::msleep(20);tick();Check(!Same(beforeFlight.Position,view->GetCameraState().Position),"W key did not move focused RMB camera");
        QWheelEvent flyingWheel(QPointF(120,100),QPointF(viewport->mapToGlobal(QPoint(120,100))),QPoint(),QPoint(0,120),Qt::RightButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(viewport,&flyingWheel);
        const auto immediatelyAfterWheel=view->GetCameraState();QThread::msleep(20);tick();
        Check(!Same(immediatelyAfterWheel.Position,view->GetCameraState().Position),"Wheel during RMB navigation cleared the held W key or look capture");
        QFocusEvent loss(QEvent::FocusOut);QCoreApplication::sendEvent(viewport,&loss);
        const auto afterLoss=view->GetCameraState();QThread::msleep(20);tick();Check(Same(afterLoss.Position,view->GetCameraState().Position),"Focus loss did not clear held W key");
        Mouse(viewport,QEvent::MouseButtonRelease,{145,80},Qt::RightButton,Qt::NoButton);
        const auto orbitPivot=view->GetCameraState().OrbitPivot;
        Mouse(viewport,QEvent::MouseButtonPress,{100,100},Qt::LeftButton,Qt::LeftButton,Qt::AltModifier);
        Mouse(viewport,QEvent::MouseMove,{130,110},Qt::NoButton,Qt::LeftButton,Qt::AltModifier);
        Mouse(viewport,QEvent::MouseButtonRelease,{130,110},Qt::LeftButton,Qt::NoButton,Qt::AltModifier);
        Check(Same(view->GetCameraState().OrbitPivot,orbitPivot),"Alt-left orbit moved its pivot");
        const auto beforePan=view->GetCameraState();
        Mouse(viewport,QEvent::MouseButtonPress,{100,100},Qt::MiddleButton,Qt::MiddleButton);
        Mouse(viewport,QEvent::MouseMove,{120,115},Qt::NoButton,Qt::MiddleButton);
        Mouse(viewport,QEvent::MouseButtonRelease,{120,115},Qt::MiddleButton,Qt::NoButton);
        Check(!Same(beforePan.Position,view->GetCameraState().Position)&&Same(view->GetCameraState().Position-beforePan.Position,view->GetCameraState().OrbitPivot-beforePan.OrbitPivot),"Middle-button pan did not move eye and pivot together");
        const float oldDistance=view->GetCameraState().OrbitDistance;
        QWheelEvent wheel(QPointF(100,100),QPointF(viewport->mapToGlobal(QPoint(100,100))),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QCoreApplication::sendEvent(viewport,&wheel);
        Check(view->GetCameraState().OrbitDistance<oldDistance,"Viewport wheel did not dolly");
        if(runtime){QKeyEvent focus(QEvent::KeyPress,Qt::Key_F,Qt::NoModifier);QCoreApplication::sendEvent(viewport,&focus);Check(Same(view->GetCameraState().OrbitPivot,{0,0,0}),"F key did not center selected triangle");}
        const auto beforeHide=view->GetCameraState();viewport->hide();tick();viewport->show();tick();
        Check(Same(beforeHide.Position,view->GetCameraState().Position),"Hidden/focus-lost viewport kept camera input active");
        if(runtime) {
            auto* scene=editor->Scenes.Scene;
            Check(CountNodes(scene)==nodeCount&&scene->GetMainCamera()==gameCamera,"Scene view camera was inserted into scene or changed Game main camera");
            auto* group=scene->CreateGameObject("Transformed Group");
            group->GetTransform()->SetLocalPosition({10,3,-4});group->GetTransform()->SetLocalScale({2,1,3});
            Check(group->GetTransform()->SetLocalRotation({0,std::sqrt(0.5f),0,std::sqrt(0.5f)}),"Cannot rotate focus fixture parent");
            auto* target=scene->CreateGameObject("Offset Mesh",group);target->GetTransform()->SetLocalPosition({1,2,0});
            target->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"11111111111111111111111111111111",1});target->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
            auto* child=scene->CreateGameObject("Subtree Mesh",target);child->GetTransform()->SetLocalPosition({0,0,2});
            child->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"11111111111111111111111111111111",1});child->AddComponent<GameSproutRuntime::IMeshRendererComponent>();
            auto* empty=scene->CreateGameObject("Empty Outlier",target);empty->GetTransform()->SetLocalPosition({100,0,0});
            auto state=view->GetCameraState();state.Position={13,5,14};state.OrbitPivot={13,5,-6};state.YawDegrees=0;state.PitchDegrees=0;state.OrbitDistance=20;
            Check(view->SetCameraState(state),"Cannot aim renderer at transformed meshes");
            pump([&]{return target->GetComponent<GameSproutRuntime::IMeshComponent>()->GetLocalBounds().Extents.X>0&&child->GetComponent<GameSproutRuntime::IMeshComponent>()->GetLocalBounds().Extents.X>0;});
            QTreeView* tree=nullptr;
            for(QWidget* window:QApplication::topLevelWidgets())if(auto* dock=window->findChild<QDockWidget*>("Hierarchy")){tree=dock->findChild<QTreeView*>("hierarchyTree");if(tree)break;}
            Check(tree,"Real Hierarchy tree missing");
            editor->Selection.Selected={scene->GetInstanceID(),mesh->GetInstanceID()};
            auto* peerDock=ide->CreateDocker("Focus Peer");Check(peerDock,"Cannot create tab activation fixture");
            Check(peerDock->AttachWidget(new QWidget()),"Cannot attach tab activation fixture");
            Check(ide->QueryDocker("Scene")->Tabify(peerDock),"Cannot tab Scene with another view");peerDock->Activate();
            nodeCount=CountNodes(scene);const auto beforeFocus=view->GetCameraState();view->Hide();QCoreApplication::processEvents();
            Check(!viewport->isVisible(),"Scene hide fixture failed");
            DoubleClickNode(tree,target->GetInstanceID());
            Check(editor->Selection.Selected==SceneObjectSelection{scene->GetInstanceID(),target->GetInstanceID()},"Hierarchy double click did not publish target selection before focus");
            Check(viewport->isVisible(),"Hierarchy double click did not show Scene view");
            Check(tree->isExpanded(NodeIndex(tree->model(),target->GetInstanceID())),"Hierarchy double click collapsed selected subtree");
            const auto focused=view->GetCameraState();const GameSproutRuntime::Vector3 expected{13,5,-6};
            Check((focused.OrbitPivot-expected).Length()<0.001f,"Hierarchy focus ignored transformed subtree geometry or included empty outlier");
            Check(focused.YawDegrees==beforeFocus.YawDegrees&&focused.PitchDegrees==beforeFocus.PitchDegrees,"Hierarchy focus changed Scene camera angle");
            const float vertical=GameSproutRuntime::DegreesToRadians(focused.VerticalFovDegrees)*0.5f;
            const float horizontal=std::atan(std::tan(vertical)*float(viewport->width())/float(viewport->height()));
            const float expectedDistance=std::sqrt(14.0f)/std::sin(std::min(vertical,horizontal))*1.2f;
            Check(std::fabs((focused.Position-expected).Length()-expectedDistance)<0.003f,"Hierarchy focus camera distance did not fit world bounds/viewport aspect");
            DoubleClickNode(tree,empty->GetInstanceID());
            Check((view->GetCameraState().OrbitPivot-GameSproutRuntime::Vector3{10,5,-206}).Length()<0.002f,"Empty object focus did not use its transformed world position");
            const auto beforeTitle=view->GetCameraState();DoubleClickNode(tree,scene->GetInstanceID());
            Check(Same(beforeTitle.Position,view->GetCameraState().Position)&&Same(beforeTitle.OrbitPivot,view->GetCameraState().OrbitPivot),"Scene title double click incorrectly reframed the scene");
            Check(CountNodes(scene)==nodeCount&&scene->GetMainCamera()==gameCamera,"Double-click framing created nodes or changed Game main camera");
            const auto after=gameCamera->GetGameObject()->GetTransform()->GetWorldMatrix();
            for(int index=0;index<16;++index)Check(after.Data[index]==gameWorld.Data[index],"Scene navigation changed Game camera matrix");
        }
        // 真实宿主先销毁 Dock/native NSView，随后才释放 Scene View 接口。
        GameSproutEditor::GameSproutEditorFinal::Release(ide,editor);ide=nullptr;QCoreApplication::processEvents();
        Check(viewportDestroyed,"IDE Release did not destroy its native Scene child");
        GameSproutEditor::GameSproutEditorFinal::Update(view,editor);
        GameSproutEditor::GameSproutEditorFinal::Release(hierarchy,editor);hierarchy=nullptr;
        GameSproutEditor::GameSproutEditorFinal::Release(view,editor);view=nullptr;
        delete runtime;runtime=nullptr;delete editor;editor=nullptr;
        std::cout<<"PASS Scene native child, independent camera, physical resize and host-before-view teardown"<<(headless?" (headless guarded)":"")<<'\n';return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<'\n';
        if(ide)GameSproutEditor::GameSproutEditorFinal::Release(ide,editor);
        if(hierarchy)GameSproutEditor::GameSproutEditorFinal::Release(hierarchy,editor);
        if(view)GameSproutEditor::GameSproutEditorFinal::Release(view,editor);
        delete runtime;delete editor;return 1;
    }
}

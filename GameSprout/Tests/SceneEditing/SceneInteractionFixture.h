#pragma once
#include "SceneEditingTestSupport.h"
#include "SceneViewport.h"
#include "SceneGizmo.h"
#include "IHierarchyUI.h"
#include <QDockWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QTest>
#include <QScreen>
#include <QCheckBox>
#include <cstring>
#include <algorithm>

class SceneInteractionGeometry final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    uint64_t GetRevision() const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::MeshRenderData& output,std::string& error) override {
        struct Vertex {float Position[3],Normal[3],Color[4],UV[2];};
        Vertex vertices[]={{{-1.5f,-1,0},{0,0,1},{.38f,.42f,.48f,1},{0,0}},{{1.5f,-1,0},{0,0,1},{.38f,.42f,.48f,1},{1,0}},{{0,1.5f,0},{0,0,1},{.38f,.42f,.48f,1},{.5f,1}}};
        if(reference.LocalID==2)for(auto& vertex:vertices)vertex.Position[0]+=1.25f;
        if(reference.LocalID==3) {
            const float points[3][2]={{-2,-2},{2,-2},{2,2}};
            for(int index=0;index<3;++index){vertices[index].Position[0]=points[index][0];vertices[index].Position[1]=points[index][1];}
        }
        const uint32_t indices[]={0,1,2};output={};output.VertexCount=3;output.VertexStride=sizeof(Vertex);output.IndexCount=3;output.IndexOffset=sizeof(vertices);output.ColorOffset=24;output.UVOffset=40;
        output.Bytes.resize(sizeof(vertices)+sizeof(indices));std::memcpy(output.Bytes.data(),vertices,sizeof(vertices));std::memcpy(output.Bytes.data()+sizeof(vertices),indices,sizeof(indices));output.Sections.push_back({0,3,0});
        GameSproutRuntime::Vector3 minimum{vertices[0].Position[0],vertices[0].Position[1],0},maximum=minimum;
        for(const auto& vertex:vertices) {minimum.X=std::min(minimum.X,vertex.Position[0]);minimum.Y=std::min(minimum.Y,vertex.Position[1]);maximum.X=std::max(maximum.X,vertex.Position[0]);maximum.Y=std::max(maximum.Y,vertex.Position[1]);}
        output.LocalBounds.Center=(minimum+maximum)*.5f;output.LocalBounds.Extents=(maximum-minimum)*.5f;error.clear();return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData& output,std::string& error) override{output={};error="No textures in interaction fixture";return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData& output,std::string& error) override{output={};output.DoubleSided=true;error.clear();return true;}
};
class SceneInteractionFixture final {
public:
    SceneInteractionFixture()=default;
    ~SceneInteractionFixture(){Shutdown();}
    void Initialize() {
        Services.Initialize();auto& editor=Services.Editor;
        IDE=static_cast<IIDEWindow*>(LoadView("IDEWindow"));editor.IDE=IDE;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(IDE,&editor)&&GameSproutEditor::GameSproutEditorFinal::Launch(IDE,&editor),"Scene editing IDE startup failed");
        for(const std::string& name:{"SceneViewUI","InspectorUI","HierarchyUI"}) {
            auto* view=LoadView(name);editor.Views[name]=view;Views.push_back(view);
        }
        for(auto* view:Views)GameSproutEditor::GameSproutEditorFinal::Before(view,&editor);
        for(auto* view:Views)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(view,&editor),"Scene editing view initialization failed");
        for(auto* view:Views)Check(GameSproutEditor::GameSproutEditorFinal::Launch(view,&editor),"Scene editing view launch failed");
        SceneView=static_cast<ISceneViewUI*>(editor.Views["SceneViewUI"]);IDE->Show();SceneView->Show();QCoreApplication::processEvents();
        for(QWidget* window:QApplication::topLevelWidgets())if(auto* host=qobject_cast<QMainWindow*>(window))if(auto* dock=host->findChild<QDockWidget*>("Scene")) {
            Host=host;SceneDock=dock;Viewport=dock->findChild<SceneViewport*>("sceneViewport");if(Viewport)break;
        }
        Check(Host&&Viewport,"Real Scene editing native viewport missing");InspectorDock=Host->findChild<QDockWidget*>("Inspector");Check(InspectorDock,"Inspector missing");
        Viewport->Initialize(Services.Editor._Runtime,&Geometry);
        Host->resize(1440,900);Host->show();Host->raise();Host->activateWindow();
        IDE->QueryDocker("Inspector")->Layout(eDockerLayout::Right);
        Check(IDE->QueryDocker("Hierarchy")->Split(IDE->QueryDocker("Scene"),Qt::Horizontal),"Cannot lay out interactive Scene");
        Check(IDE->ResizeDockers({IDE->QueryDocker("Hierarchy"),IDE->QueryDocker("Scene"),IDE->QueryDocker("Inspector")},{18,55,27},Qt::Horizontal),"Cannot size interactive Scene");
        IDE->QueryDocker("Scene")->Activate();Viewport->setFocus();
        auto camera=SceneView->GetCameraState();camera.Position={0,0,8};camera.OrbitPivot={0,0,0};camera.YawDegrees=0;camera.PitchDegrees=0;camera.OrbitDistance=8;
        Check(SceneView->SetCameraState(camera),"Cannot initialize test Scene camera");
        if(auto* grid=SceneDock->findChild<QCheckBox*>("gridCheck"))grid->setChecked(false);
        Pump([&]{GameSproutRuntime::RenderedCamera frame;return SceneView->IsRenderingAvailable()&&Viewport->GetRenderedCamera(frame)&&frame.Width==qRound(Viewport->width()*Viewport->devicePixelRatioF());});
    }
    void Tick() {
        Services.Tick();
        for(auto* view:Views)GameSproutEditor::GameSproutEditorFinal::Update(view,&Services.Editor);
        QCoreApplication::processEvents();
    }
    void Pump(const std::function<bool()>& ready) {
        QElapsedTimer time;time.start();do{Tick();if(ready())return;QThread::msleep(3);}while(time.elapsed()<20000);
        throw std::runtime_error("Scene interaction timed out: "+(SceneView?SceneView->GetRenderError():std::string("no Scene view")));
    }
    void Settle(int milliseconds=30) {QElapsedTimer time;time.start();do{Tick();QThread::msleep(2);}while(time.elapsed()<milliseconds);}
    void Shutdown() {
        for(auto iterator=Views.rbegin();iterator!=Views.rend();++iterator)GameSproutEditor::GameSproutEditorFinal::Release(*iterator,&Services.Editor);
        Views.clear();Services.Editor.Views.clear();SceneView=nullptr;
        if(IDE){GameSproutEditor::GameSproutEditorFinal::Release(IDE,&Services.Editor);IDE=nullptr;}
        Host=nullptr;Viewport=nullptr;SceneDock=nullptr;InspectorDock=nullptr;
        Services.Shutdown();
    }
    GameSproutRuntime::IGameObject* Mesh(const std::string& name,GameSproutRuntime::Vector3 position,uint64_t localID=1,GameSproutRuntime::IGameObject* parent=nullptr) {
        auto* object=Services.Scenes->GetActiveScene()->CreateGameObject(name,parent);object->GetTransform()->SetLocalPosition(position);
        object->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh({"11223344556677889900aabbccddeeff",localID});
        Check(object->AddComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterial(0,{"ffeeddccbbaa00998877665544332211",1}),"Cannot assign test material");return object;
    }
    void Select(GameSproutRuntime::IGameObject* object) {
        Services.Selection->SetSceneObjectSelection(object?SceneObjectSelection{Services.Scenes->GetActiveScene()->GetInstanceID(),object->GetInstanceID()}:SceneObjectSelection{});Settle();
    }
    SceneGizmoView Frame() const {
        GameSproutRuntime::RenderedCamera frame;Check(Viewport->GetRenderedCamera(frame),"No successfully submitted frame available");return {frame.Camera,frame.Width,frame.Height};
    }
    QPointF Logical(GameSproutRuntime::Vector2 physical) const {const auto frame=Frame();return {physical.X*Viewport->width()/frame.Width,physical.Y*Viewport->height()/frame.Height};}
    GameSproutRuntime::Vector2 Project(GameSproutRuntime::Vector3 world) const {GameSproutRuntime::Vector2 pixel;Check(SceneGizmo::Project(Frame(),world,pixel),"Cannot project interaction fixture");return pixel;}
    void ClickPixel(GameSproutRuntime::Vector2 pixel) {
        const QPoint position=Logical(pixel).toPoint();QWindow* window=Viewport->windowHandle();Check(window,"Viewport has no native Qt window");
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,position);Settle();
    }
    void Click(GameSproutRuntime::Vector3 world) {ClickPixel(Project(world));}
    void Key(int key,Qt::KeyboardModifiers modifiers=Qt::NoModifier) {Viewport->setFocus();QTest::keyClick(Viewport->windowHandle(),static_cast<Qt::Key>(key),modifiers);Settle();}
    QImage Capture() const {
        auto* screen=Host->screen();Check(screen,"No screen for native Metal capture");const QPoint origin=Viewport->mapTo(Host,QPoint());
        auto image=screen->grabWindow(Host->winId(),origin.x(),origin.y(),Viewport->width(),Viewport->height()).toImage();Check(!image.isNull(),"Native Metal capture failed");return image;
    }
    SceneInteractionGeometry Geometry;
    SceneEditingServices Services;
    IIDEWindow* IDE=nullptr;
    ISceneViewUI* SceneView=nullptr;
    std::vector<GameSproutEditor::IGameSproutView*> Views;
    QMainWindow* Host=nullptr;
    QDockWidget* SceneDock=nullptr;
    QDockWidget* InspectorDock=nullptr;
    SceneViewport* Viewport=nullptr;
};

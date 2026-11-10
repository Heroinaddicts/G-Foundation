#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include "ISceneManager.h"
#include "IObjectEditing.h"
#include "IUIAuthoring.h"
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
#include <QCheckBox>
#include <QLineF>
#include <QScrollArea>
#include <QToolButton>
#include <QToolTip>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTabBar>
#include <QScrollBar>
#include <cmath>
#include <cstring>

class SceneFixture final : public ISceneManager {
public:
    GameSproutRuntime::IScene* Scene=nullptr;
    std::string Path;
    GameSproutRuntime::ITransformComponent* DragTarget=nullptr;
    GameSproutRuntime::Vector3 DragStart;
    unsigned DragPreviews=0,DragCommits=0;
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
    bool BeginTransformEdit(uint64_t id,const std::string&,std::string&) override {
        for(std::size_t index=0;index<Scene->GetChildCount();++index) {
            auto* node=Scene->GetChild(index);
            if(node->GetInstanceID()!=id)continue;
            DragTarget=node->GetTransform();DragStart=DragTarget->GetLocalPosition();return true;
        }
        return false;
    }
    bool PreviewTransformEdit(const SceneTransformState& value,std::string&) override {
        if(!DragTarget)return false;
        ++DragPreviews;DragTarget->SetLocalPosition(value.Position);return true;
    }
    bool CommitTransformEdit(std::string&) override {++DragCommits;DragTarget=nullptr;return true;}
    bool CancelTransformEdit(std::string&) override {
        if(DragTarget)DragTarget->SetLocalPosition(DragStart);
        DragTarget=nullptr;return true;
    }
    bool HasTransformEdit() const override{return DragTarget!=nullptr;}
    bool RegisterSceneListener(ISceneListener*) override{return true;}
    void UnregisterSceneListener(ISceneListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class SelectionFixture final : public ISelectionManager {
public:
    SceneObjectSelection Selected;
    std::vector<ISelectionListener*> Listeners;
    std::vector<ProjectAssetSelection> GetAssetSelection() const override{return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>&) override{}
    SceneObjectSelection GetSceneObjectSelection() const override{return Selected;}
    void SetSceneObjectSelection(const SceneObjectSelection& value) override{Selected=value;for(auto* listener:Listeners)listener->OnSceneObjectSelectionChanged(value);}
    bool RegisterSelectionListener(ISelectionListener* listener) override{if(!listener||std::find(Listeners.begin(),Listeners.end(),listener)!=Listeners.end())return false;Listeners.push_back(listener);return true;}
    void UnregisterSelectionListener(ISelectionListener* listener) override{std::erase(Listeners,listener);}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class HostFixture final : public IRuntimeHost {
public:
    bool Playing=false,FrameActive=false;
    std::vector<IRuntimeFrameListener*> Listeners;
    uint64_t FrameNumber=0;
    void SetSimulationPlaying(bool value)override{Playing=value;for(auto* listener:Listeners)listener->OnSimulationPlayingChanged(value);}
    bool IsSimulationPlaying()const override{return Playing;}
    bool IsFrameActive()const override{return FrameActive;}
    void ResetClock()override{}
    bool RegisterFrameListener(IRuntimeFrameListener* listener)override{if(!listener||std::find(Listeners.begin(),Listeners.end(),listener)!=Listeners.end())return false;Listeners.push_back(listener);return true;}
    void UnregisterFrameListener(IRuntimeFrameListener* listener)override{std::erase(Listeners,listener);}
    void Frame() {
        const RuntimeFrameEvent event{++FrameNumber,1.0f/60.0f,Playing};
        const auto snapshot=Listeners;
        FrameActive=true;
        for(auto* listener:snapshot)
            if(std::find(Listeners.begin(),Listeners.end(),listener)!=Listeners.end())listener->OnRuntimeFrame(event);
        FrameActive=false;
    }
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class RectAuthoringFixture final : public IUIAuthoring {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    const IRuntimeHost* Host=nullptr;
    std::uint64_t ObjectID=0;
    UIRectAuthoringValues Original;
    unsigned Previews=0,Commits=0;
    bool CanCreate(std::uint64_t,std::uint64_t,std::string&) const override{return false;}
    bool CreateControl(std::uint64_t,std::uint64_t,UIControlTemplate,std::uint64_t&,std::string&) override{return false;}
    bool SetText(const ObjectEditTarget&,const UITextAuthoringValues&,std::string&) override{return false;}
    bool SetButtonInteractable(const ObjectEditTarget&,bool,std::string&) override{return false;}
    bool BeginRectEdit(const ObjectEditTarget& target,std::string&) override {
        auto* object=Runtime?Runtime->QueryGameObject(target.ObjectID):nullptr;
        auto* rect=object?object->GetComponent<GameSproutRuntime::IRectTransformComponent>():nullptr;
        if(ObjectID||!rect||rect->GetComponentInstanceID()!=target.ComponentID)return false;
        ObjectID=target.ObjectID;
        Original={rect->GetAnchoredPosition(),rect->GetSizeDelta(),object->GetTransform()->GetLocalRotation()};
        return true;
    }
    bool PreviewRectEdit(const UIRectAuthoringValues& values,std::string&) override {++Previews;return Write(values);}
    bool CommitRectEdit(const UIRectAuthoringValues& values,std::string&) override {
        ++Commits;const bool written=Write(values);ObjectID=0;return written;
    }
    bool CancelRectEdit(std::string&) override {
        const bool restored=!ObjectID||Write(Original);ObjectID=0;return restored;
    }
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
private:
    bool Write(const UIRectAuthoringValues& values) {
        if(Host&&Host->IsFrameActive())return false;
        auto* object=Runtime?Runtime->QueryGameObject(ObjectID):nullptr;
        auto* rect=object?object->GetComponent<GameSproutRuntime::IRectTransformComponent>():nullptr;
        if(!rect||!object->GetTransform()->SetLocalRotation(values.LocalRotation))return false;
        rect->SetAnchoredPosition(values.AnchoredPosition);
        rect->SetSizeDelta(values.SizeDelta);
        return true;
    }
};
class WindowEditor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit WindowEditor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){UIAuthoring.Runtime=runtime;UIAuthoring.Host=&Host;}
    SceneFixture Scenes;
    HostFixture Host;
    SelectionFixture Selection;
    RectAuthoringFixture UIAuthoring;
    IIDEWindow* IDE=nullptr;
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        if(name=="RuntimeHost")return const_cast<HostFixture*>(&Host);
        if(name=="SceneManager")return const_cast<SceneFixture*>(&Scenes);
        if(name=="SelectionManager")return const_cast<SelectionFixture*>(&Selection);
        if(name=="UIAuthoring")return const_cast<RectAuthoringFixture*>(&UIAuthoring);
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
        const Vertex vertices[]={{{-1,-1,0},{0,0,-1},{1,0.2f,0.1f,1},{0,0}},{{1,-1,0},{0,0,-1},{0.1f,1,0.2f,1},{1,0}},{{0,1,0},{0,0,-1},{0.2f,0.1f,1,1},{0.5f,1}}};
        const std::uint32_t indices[]={0,2,1};
        data={};data.VertexCount=3;data.VertexStride=sizeof(Vertex);data.IndexCount=3;data.IndexOffset=sizeof(vertices);data.ColorOffset=24;data.UVOffset=40;
        data.Bytes.resize(sizeof(vertices)+sizeof(indices));std::memcpy(data.Bytes.data(),vertices,sizeof(vertices));std::memcpy(data.Bytes.data()+sizeof(vertices),indices,sizeof(indices));
        data.Sections.push_back({0,3,0});data.LocalBounds.Extents={1,1,0};error.clear();return true;
    }
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData& data,std::string& error) override{data={};error="No texture required by this triangle";return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData& data,std::string& error) override{data={};data.DoubleSided=true;error.clear();return true;}
};
class FeatureToolFixture final : public ISceneTool {
public:
    bool Active=false,Dragging=false;
    unsigned Activations=0,Cancellations=0;
    void OnSceneToolActivated(bool value) override {Active=value;++Activations;}
    void DrawSceneTool(const SceneToolContext&,GameSproutRuntime::DebugDrawList&) override {}
    bool PressSceneTool(const SceneToolContext&,const SceneToolPointer&) override {Dragging=true;return true;}
    bool MoveSceneTool(const SceneToolContext&,const SceneToolPointer&) override {return Dragging;}
    bool ReleaseSceneTool(const SceneToolContext&,const SceneToolPointer&) override {const bool result=Dragging;Dragging=false;return result;}
    void CancelSceneToolDrag() override {if(Dragging)++Cancellations;Dragging=false;}
    bool IsSceneToolDragging() const override {return Dragging;}
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
            mesh->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
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
        GameSproutEditor::GameSproutEditorFinal::Finished(view,editor);
        GameSproutEditor::GameSproutEditorFinal::Finished(hierarchy,editor);
        ide->Show();view->Show();QCoreApplication::processEvents();
        SceneViewport* viewport=nullptr;
        for(QWidget* window:QApplication::topLevelWidgets())if(auto* dock=window->findChild<QDockWidget*>("Scene")){viewport=dock->findChild<SceneViewport*>("sceneViewport");if(viewport)break;}
        Check(viewport,"Promoted native sceneViewport missing");
        if(runtime){viewport->Initialize(runtime,&resources);viewport->BindEditingServices(&editor->Scenes,&editor->Selection,&editor->UIAuthoring);}
        QObject::connect(viewport,&QObject::destroyed,&app,[&]{viewportDestroyed=true;});
        auto* host=viewport->window();host->resize(880,650);host->raise();host->activateWindow();QApplication::setActiveWindow(host);viewport->setFocus();QCoreApplication::processEvents();
        auto* sceneDock=host->findChild<QDockWidget*>("Scene");Check(sceneDock,"Scene dock missing for compact UI checks");
        auto* content=sceneDock->widget();
        Check(!content->findChild<QLabel*>("inputHelpLabel")&&!content->findChild<QLabel*>("statusLabel"),"Permanent help/statistics still consume Scene viewport height");
        auto* strip=content->findChild<QScrollArea*>("toolScrollArea");
        Check(strip&&strip->height()<=46&&viewport->height()>=content->height()-48,"Compact Scene toolbar reserves multiple permanent rows");
        auto* reuseShadows=content->findChild<QCheckBox*>("reuseNavigationShadowsCheck");
        Check(reuseShadows&&reuseShadows->isChecked()&&view->IsShadowReuseWhileNavigatingEnabled(),
            "Scene toolbar must enable navigation shadow reuse by default");
        reuseShadows->click();
        Check(!reuseShadows->isChecked()&&!view->IsShadowReuseWhileNavigatingEnabled(),
            "Scene shadow toggle did not update the public View policy");
        view->SetShadowReuseWhileNavigatingEnabled(true);
        Check(reuseShadows->isChecked()&&view->IsShadowReuseWhileNavigatingEnabled(),
            "Public Scene shadow setter did not update the toolbar toggle");
        auto* help=host->findChild<QToolButton*>("sceneHelpButton");
        auto* statistics=host->findChild<QToolButton*>("statisticsButton");
        Check(help&&statistics&&help->isVisible(),"Scene title actions are not available");
        help->click();QCoreApplication::processEvents();
        Check(QToolTip::text().contains("WASD")&&QToolTip::text().contains("Delete"),"Scene question button did not open complete mouse-position help");
        QToolTip::hideText();
        auto* snapButton=content->findChild<QToolButton*>("snapSettingsButton");
        Check(snapButton,"Snap popup entry missing");snapButton->click();QCoreApplication::processEvents();
        auto* snapPopup=content->findChild<QWidget*>("SceneSnapPopup");
        auto* moveSnap=snapPopup?snapPopup->findChild<QDoubleSpinBox*>("moveSnapSpin"):nullptr;
        Check(snapPopup&&snapPopup->isVisible()&&moveSnap,"Snap controls are not available from the compact toolbar");
        moveSnap->setValue(.25);
        if(headless)snapPopup->grab().save(QStringLiteral("/tmp/gamesprout-scene-snap-popup.png"));
        QKeyEvent popupEscape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(moveSnap,&popupEscape);
        Check(!snapPopup->isVisible(),"Escape inside a snap spinbox did not dismiss its popup");
        statistics->click();QCoreApplication::processEvents();
        auto* statisticsPopup=content->findChild<QWidget*>("SceneStatisticsPopup");
        Check(statisticsPopup&&statisticsPopup->isVisible(),"Render statistics did not open on demand");
        if(headless)statisticsPopup->grab().save(QStringLiteral("/tmp/gamesprout-scene-statistics-popup.png"));
        statisticsPopup->hide();
        auto* peer=ide->CreateDocker("Compact title peer");Check(peer&&ide->QueryDocker("Scene")->Tabify(peer),"Cannot tabify Scene title controls");
        peer->Activate();QCoreApplication::processEvents();QCoreApplication::processEvents();
        Check(!help->isVisible(),"Scene help leaked into a different active Docker tab");
        view->Show();QCoreApplication::processEvents();QCoreApplication::processEvents();
        Check(help->isVisible(),"Scene help did not follow its active tab");
        auto* peerDock=host->findChild<QDockWidget*>("Compact title peer");Check(peerDock,"Peer dock missing");peerDock->hide();
        QCoreApplication::processEvents();QCoreApplication::processEvents();
        if(headless){
            host->grab().save(QStringLiteral("/tmp/gamesprout-scene-compact-ui.png"));
            sceneDock->setFloating(true);
            for(int width:{320,480}) {
                sceneDock->resize(width,440);QCoreApplication::processEvents();QCoreApplication::processEvents();
                Check(help->isVisible()&&strip->height()<=46&&viewport->height()>=content->height()-48,
                    "Narrow/floating Scene title or viewport allocation regressed");
                auto* scroll=strip->horizontalScrollBar();scroll->setValue(scroll->maximum());QCoreApplication::processEvents();
                auto* retry=content->findChild<QToolButton*>("retryRenderButton");
                Check(retry&&strip->viewport()->rect().contains(retry->mapTo(strip->viewport(),retry->rect().center())),
                    "Narrow toolbar clips controls without an accessible overflow scroll: strip="+std::to_string(strip->width())+
                    " content="+std::to_string(strip->widget()->width())+" range="+std::to_string(scroll->maximum())+
                    " point="+std::to_string(retry?retry->mapTo(strip->viewport(),retry->rect().center()).x():-1));
                scroll->setValue(0);
                sceneDock->grab().save(QStringLiteral("/tmp/gamesprout-scene-compact-%1.png").arg(width));
            }
            sceneDock->setFloating(false);QCoreApplication::processEvents();QCoreApplication::processEvents();
        }
        host->activateWindow();QApplication::setActiveWindow(host);viewport->setFocus();QCoreApplication::processEvents();
        auto* colliders=host->findChild<QCheckBox*>("allCollidersCheck");
        Check(colliders&&!colliders->isChecked()&&!viewport->Editing().ShowAllColliders(),"Scene must default to selected-only collider wires");
        colliders->setChecked(true);Check(viewport->Editing().ShowAllColliders(),"Scene .ui checkbox did not enable all collider wires");
        colliders->setChecked(false);Check(!viewport->Editing().ShowAllColliders(),"Scene collider filter did not return to selected-only");
        FeatureToolFixture firstTool,secondTool;
        Check(view->RegisterSceneTool(&firstTool,&firstTool)&&!view->RegisterSceneTool(&firstTool,&firstTool)
            &&view->RegisterSceneTool(&secondTool,&secondTool),"Feature Scene tool registration did not enforce identities");
        Check(view->ActivateSceneTool(&firstTool)&&firstTool.Active&&view->GetActiveSceneTool()==&firstTool,"Feature Scene tool did not activate");
        Check(view->ActivateSceneTool(&secondTool)&&!firstTool.Active&&secondTool.Active,"Feature tool replacement kept two active tools");
        QKeyEvent switchTool(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier);QCoreApplication::sendEvent(viewport,&switchTool);
        Check(!secondTool.Active&&!view->GetActiveSceneTool(),"Transform shortcut did not deactivate feature Scene tool");
        Check(view->ActivateSceneTool(&firstTool),"Feature tool did not reactivate");
        editor->Selection.SetSceneObjectSelection(editor->Selection.Selected);
        Check(!view->GetActiveSceneTool(),"Scene selection event did not deactivate feature tool");
        Check(view->ActivateSceneTool(&secondTool),"Feature tool reactivation failed");
        view->UnregisterSceneTools(&secondTool);Check(!secondTool.Active&&!view->ActivateSceneTool(&secondTool),"Unregistered feature retained activation access");
        view->UnregisterSceneTools(&firstTool);
        const auto tick=[&]{QCoreApplication::processEvents();if(runtime)runtime->Update(0);editor->Host.Frame();QCoreApplication::processEvents();};
        const auto pump=[&](const std::function<bool()>& done,const char* stage="viewport"){
            QElapsedTimer timer;timer.start();
            do{tick();if(done())return;QThread::msleep(5);}while(timer.elapsed()<15000);
            throw std::runtime_error(std::string("Native scene timed out at ")+stage+": "+view->GetRenderError()+
                " (requested UI "+std::to_string(viewport->Editing().UIScreenPreviewObjectID())+
                ", presented UI "+std::to_string(viewport->GetPresentedUIPreviewObjectID())+
                ", UI draws "+std::to_string(viewport->GetStatistics().UIDrawCalls)+")");
        };
        if(headless) {
            tick();Check(!view->IsRenderingAvailable()&&spy.NativeCalls==0&&view->GetRenderError().find("离屏")!=std::string::npos,"Headless passed a fake window to Runtime");
        }else {
            auto state=view->GetCameraState();state.Position={0,0,-3};state.OrbitPivot={0,0,0};state.YawDegrees=0;state.PitchDegrees=0;state.OrbitDistance=3;
            Check(view->SetCameraState(state),"Cannot set Scene camera override");
            pump([&]{return view->IsRenderingAvailable()&&viewport->GetStatistics().Triangles>=1;});
            auto stats=viewport->GetStatistics();Check(stats.DrawCalls>0&&stats.VisibleObjects>0,"Native test only cleared and did not draw triangle");
            std::cout<<"Native first frame "<<stats.Width<<'x'<<stats.Height<<" logical="<<viewport->width()<<'x'<<viewport->height()<<" dpr="<<viewport->devicePixelRatioF()<<'\n';
            Check(std::abs(int(stats.Width)-qRound(viewport->width()*viewport->devicePixelRatioF()))<=2&&std::abs(int(stats.Height)-qRound(viewport->height()*viewport->devicePixelRatioF()))<=2,"Native SurfaceFrame did not match physical Retina pixels");
            const QPoint pivot(viewport->width()/2,viewport->height()/2);
            const auto moveDrag=[&](int x) {Mouse(viewport,QEvent::MouseMove,pivot+QPoint(x,0),Qt::NoButton,Qt::LeftButton);};
            const auto startDrag=[&] {
                Mouse(viewport,QEvent::MouseButtonPress,pivot,Qt::LeftButton,Qt::LeftButton);
                Check(editor->Scenes.HasTransformEdit(),"Center gizmo did not open an edit");
            };
            startDrag();
            for(int x:{8,15,20})moveDrag(x);
            Check(editor->Scenes.DragPreviews==0,"Mouse input edited the document before the host frame");
            tick();
            const auto previewed=mesh->GetTransform()->GetLocalPosition();
            Check(editor->Scenes.DragPreviews==1&&std::fabs(previewed.X)>.01f,"Drag inputs did not coalesce into one preview");
            moveDrag(28);
            Mouse(viewport,QEvent::MouseButtonRelease,pivot+QPoint(42,0),Qt::LeftButton,Qt::NoButton);
            Check(editor->Scenes.DragPreviews==2&&editor->Scenes.DragCommits==1&&
                std::fabs(mesh->GetTransform()->GetLocalPosition().X)>std::fabs(previewed.X),"Release lost the final pointer position");
            mesh->GetTransform()->SetLocalPosition({});tick();
            startDrag();moveDrag(40);
            QKeyEvent cancelDrag(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
            QCoreApplication::sendEvent(viewport,&cancelDrag);tick();
            Check(editor->Scenes.DragPreviews==2&&editor->Scenes.DragCommits==1&&
                Same(mesh->GetTransform()->GetLocalPosition(),{}),"Cancel applied a queued preview after restoring the original transform");
            auto* uiScene=editor->Scenes.Scene;
            auto* canvas=uiScene->CreateGameObject("Screen Canvas");
            Check(canvas&&canvas->AddComponent<GameSproutRuntime::ICanvasComponent>()&&
                canvas->AddComponent<GameSproutRuntime::IRectTransformComponent>(),"Cannot assemble screen Canvas");
            auto* control=uiScene->CreateGameObject("Image",canvas);
            Check(control&&control->AddComponent<GameSproutRuntime::IRectTransformComponent>()&&
                control->AddComponent<GameSproutRuntime::IImageComponent>(),"Cannot assemble screen control");
            auto* rect=control->GetComponent<GameSproutRuntime::IRectTransformComponent>();
            rect->SetSizeDelta({400,400});
            editor->Selection.SetSceneObjectSelection({uiScene->GetInstanceID(),canvas->GetInstanceID()});
            pump([&]{return viewport->GetPresentedUIPreviewObjectID()==canvas->GetInstanceID()&&viewport->GetStatistics().UIDrawCalls>0;},"Canvas selection");
            GameSproutRuntime::DebugDrawList worldTools;
            viewport->Editing().BuildDraw(uiScene,worldTools);
            Check(worldTools.Lines.empty()&&worldTools.Triangles.empty(),
                "Screen Canvas drew world-space transform handles");
            const auto canvasCamera=view->GetCameraState();
            Check(view->FocusSelection()&&Same(view->GetCameraState().Position,canvasCamera.Position),
                "Framing a screen Canvas moved the world camera");
            Mouse(viewport,QEvent::MouseButtonPress,pivot,Qt::LeftButton,Qt::LeftButton);
            Mouse(viewport,QEvent::MouseButtonRelease,pivot,Qt::LeftButton,Qt::NoButton);
            Check(!editor->Scenes.HasTransformEdit()&&editor->Selection.Selected.ObjectID==control->GetInstanceID(),
                "Screen control selection entered the 3D transform gizmo");
            pump([&]{return viewport->GetPresentedUIPreviewObjectID()==control->GetInstanceID();},"UI control selection");
            const auto rectGeometry=[&] {
                GameSproutRuntime::RenderedCamera camera;
                GameSproutRuntime::UIScreenPreviewGeometry geometry;
                Check(viewport->GetRenderedCamera(camera)&&runtime->GetUIScreenPreviewGeometry(uiScene,control->GetInstanceID(),
                    {float(camera.Width),float(camera.Height)},geometry),"Cannot read the presented Rect geometry");
                return geometry;
            };
            const auto logical=[&](GameSproutRuntime::Vector2 pixel) {
                GameSproutRuntime::RenderedCamera camera;
                Check(viewport->GetRenderedCamera(camera),"Cannot read the presented viewport");
                return QPointF(pixel.X*viewport->width()/camera.Width,pixel.Y*viewport->height()/camera.Height);
            };
            const auto pressUI=[&](QPointF point){Mouse(viewport,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);};
            const auto dragUI=[&](QPointF point){Mouse(viewport,QEvent::MouseMove,point,Qt::NoButton,Qt::LeftButton);};
            const auto releaseUI=[&](QPointF point){Mouse(viewport,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);};
            const auto shortcut=[&](int key){QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier);QCoreApplication::sendEvent(viewport,&event);};
            shortcut(Qt::Key_W);tick();
            const auto xArrow=logical(rectGeometry().Pivot)+QPointF(36,0);
            pressUI(xArrow);dragUI(xArrow+QPointF(12,8));dragUI(xArrow+QPointF(24,16));
            Check(editor->UIAuthoring.Previews==0,"W pointer events did not coalesce before the frame");
            tick();
            const float previewX=rect->GetAnchoredPosition().X;
            Check(editor->UIAuthoring.Previews==1&&previewX>0&&std::abs(rect->GetAnchoredPosition().Y)<0.001f,
                "W red arrow did not constrain movement to X");
            releaseUI(xArrow+QPointF(32,22));tick();
            Check(editor->UIAuthoring.Commits==1&&rect->GetAnchoredPosition().X>previewX,
                "W did not commit exactly once at the release coordinate");

            auto* rectTool=content->findChild<QToolButton*>("rectToolButton");
            Check(rectTool,"Scene Rect Tool is missing from the .ui toolbar");
            shortcut(Qt::Key_T);tick();
            Check(rectTool->isChecked(),"T did not activate Rect Tool");
            const auto interior=logical(rectGeometry().Pivot);
            pressUI(interior);dragUI(interior+QPointF(10,12));releaseUI(interior+QPointF(15,20));tick();
            Check(editor->UIAuthoring.Commits==2&&rect->GetAnchoredPosition().Y<0,"T did not move the Rect from its interior");

            const auto beforeResize=rectGeometry();
            const auto bottomRight=logical(beforeResize.Corners[2]);
            pressUI(bottomRight);dragUI(bottomRight+QPointF(16,14));releaseUI(bottomRight+QPointF(24,20));tick();
            const auto afterResize=rectGeometry();
            Check(editor->UIAuthoring.Commits==3&&afterResize.Size.X>beforeResize.Size.X&&afterResize.Size.Y>beforeResize.Size.Y&&
                QLineF(logical(beforeResize.Corners[0]),logical(afterResize.Corners[0])).length()<0.25,
                "T corner resize moved the opposite corner or did not grow the Rect");

            const auto rotationPivot=logical(afterResize.Pivot);
            const auto corner=logical(afterResize.Corners[2]);
            const auto ray=corner-rotationPivot;
            const auto rotationStart=corner+ray*(15.0/std::hypot(ray.x(),ray.y()));
            const auto radius=rotationStart-rotationPivot;
            const auto rotated=rotationPivot+QPointF(-radius.y(),radius.x());
            pressUI(rotationStart);dragUI(rotated);releaseUI(rotated);tick();
            Check(editor->UIAuthoring.Commits==4&&std::abs(control->GetTransform()->GetLocalRotation().Z)>0.5f,
                "T corner-outside drag did not rotate the UI");
            shortcut(Qt::Key_W);
            Check(uiScene->DestroyGameObject(canvas),"Cannot remove screen Canvas fixture");
            editor->Selection.SetSceneObjectSelection({uiScene->GetInstanceID(),mesh->GetInstanceID()});
            pump([&]{return viewport->GetPresentedUIPreviewObjectID()==0&&viewport->GetStatistics().UIDrawCalls==0;},"return to world selection");
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
            auto state=view->GetCameraState();state.Position={13,5,-26};state.OrbitPivot={13,5,-6};state.YawDegrees=0;state.PitchDegrees=0;state.OrbitDistance=20;
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
        editor->Host.Frame();QCoreApplication::processEvents();
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

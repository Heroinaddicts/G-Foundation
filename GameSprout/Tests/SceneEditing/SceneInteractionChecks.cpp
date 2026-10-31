#include "SceneInteractionFixture.h"
#include "Math/TransformMath.h"
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>

static GameSproutRuntime::Vector3 WorldPoint(const GameSproutRuntime::Mat4& matrix,GameSproutRuntime::Vector3 point={}) {
    const auto value=matrix*GameSproutRuntime::Vector4{point.X,point.Y,point.Z,1};return {value.X,value.Y,value.Z};
}
static GameSproutRuntime::Quaternion AxisRotation(GameSproutRuntime::Vector3 axis,float degrees) {
    Check(axis.TryNormalize(axis),"Invalid test rotation axis");const float angle=GameSproutRuntime::DegreesToRadians(degrees)*.5f;return {axis.X*std::sin(angle),axis.Y*std::sin(angle),axis.Z*std::sin(angle),std::cos(angle)};
}
static GameSproutRuntime::Quaternion RotationProduct(GameSproutRuntime::Quaternion first,GameSproutRuntime::Quaternion second) {
    return {first.W*second.X+first.X*second.W+first.Y*second.Z-first.Z*second.Y,
            first.W*second.Y-first.X*second.Z+first.Y*second.W+first.Z*second.X,
            first.W*second.Z+first.X*second.Y-first.Y*second.X+first.Z*second.W,
            first.W*second.W-first.X*second.X-first.Y*second.Y-first.Z*second.Z};
}
static float RotationDifferenceDegrees(GameSproutRuntime::Quaternion first,GameSproutRuntime::Quaternion second) {
    const float dot=std::clamp(std::fabs(first.X*second.X+first.Y*second.Y+first.Z*second.Z+first.W*second.W),0.f,1.f);
    return GameSproutRuntime::RadiansToDegrees(2*std::acos(dot));
}
static std::size_t SceneObjectCount(GameSproutRuntime::IGameObject* object) {
    std::size_t result=1;for(std::size_t index=0;index<object->GetChildCount();++index)result+=SceneObjectCount(object->GetChild(index));return result;
}
static void CheckInspectorPosition(SceneInteractionFixture& fixture,GameSproutRuntime::IGameObject& object) {
    auto* id=fixture.InspectorDock->findChild<QLineEdit*>("localIDEdit");
    auto* x=fixture.InspectorDock->findChild<QLineEdit*>("transformPositionXEdit");
    auto* y=fixture.InspectorDock->findChild<QLineEdit*>("transformPositionYEdit");
    auto* z=fixture.InspectorDock->findChild<QLineEdit*>("transformPositionZEdit");
    Check(id&&id->text().toULongLong()==object.GetInstanceID()&&x&&y&&z,"Picked object was not bound to the real Inspector");
    Check(CloseVector({x->text().toFloat(),y->text().toFloat(),z->text().toFloat()},object.GetTransform()->GetLocalPosition(),.001f),"Inspector did not update the current Runtime preview");
}
static void SetMode(SceneInteractionFixture& fixture,const char* name,int index) {
    auto* combo=fixture.SceneDock->findChild<QComboBox*>(name);Check(combo,std::string("Missing Scene editing mode: ")+name);
    combo->setCurrentIndex(index);fixture.Viewport->setFocus();fixture.Settle();
}
static void CheckTool(SceneInteractionFixture& fixture,const char* name) {
    auto* button=fixture.SceneDock->findChild<QToolButton*>(name);Check(button&&button->isChecked(),std::string("Viewport shortcut did not synchronize toolbar tool: ")+name);
}
static SceneGizmo ConfigureGizmo(SceneInteractionFixture& fixture,GameSproutRuntime::IGameObject& object,SceneGizmoSettings settings) {
    const auto transform=ReadTransform(object);SceneGizmoTarget target;target.Local={transform.Position,transform.Rotation,transform.Scale};
    if(object.Parent())target.ParentWorld=object.Parent()->GetTransform()->GetWorldMatrix();
    for(auto* ancestor=object.Parent();ancestor;ancestor=ancestor->Parent()) {
        target.ParentRotation=RotationProduct(ancestor->GetTransform()->GetLocalRotation(),target.ParentRotation);
        Check(target.ParentRotation.TryNormalize(target.ParentRotation),"Cannot compose test ancestor orientation");
    }
    GameSproutRuntime::Bounds bounds;
    if(fixture.Viewport->Renderer()->GetWorldBounds(fixture.Services.Scenes->GetActiveScene(),object.GetInstanceID(),bounds)){target.BoundsCenterWorld=bounds.Center;target.HasBoundsCenter=true;}
    const auto frame=fixture.Frame();const float dpi=float(frame.Height)/fixture.Viewport->height();settings.SizePixels*=dpi;settings.HitRadiusPixels*=dpi;
    SceneGizmo result;std::string error;Check(result.Update(target,frame,settings,error),error);return result;
}
static GameSproutRuntime::Vector2 AxisPixel(SceneInteractionFixture& fixture,SceneGizmo& gizmo,int axis) {
    auto pixel=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(axis)*(gizmo.GetHandleWorldSize()*.75f));
    const SceneGizmoHandle handles[]={SceneGizmoHandle::X,SceneGizmoHandle::Y,SceneGizmoHandle::Z};
    Check(gizmo.Hover(pixel)==handles[axis],"Test could not find the requested visible axis");return pixel;
}
static GameSproutRuntime::Vector3 RingPoint(SceneInteractionFixture& fixture,SceneGizmo& gizmo,int axis) {
    const SceneGizmoHandle handles[]={SceneGizmoHandle::X,SceneGizmoHandle::Y,SceneGizmoHandle::Z};
    for(int step=1;step<90;++step) {
        const float angle=step*GameSproutRuntime::Pi/45;
        const auto point=gizmo.GetPivotWorld()+(gizmo.GetAxis((axis+1)%3)*std::cos(angle)+gizmo.GetAxis((axis+2)%3)*std::sin(angle))*gizmo.GetHandleWorldSize();
        if(gizmo.Hover(fixture.Project(point))==handles[axis])return point;
    }
    throw std::runtime_error("Test could not find a distinct rotation ring");
}
enum class EndGesture {Commit,Escape,FocusLoss};
static void DragGizmo(SceneInteractionFixture& fixture,GameSproutRuntime::Vector2 begin,GameSproutRuntime::Vector2 end,
    Qt::KeyboardModifiers modifiers=Qt::NoModifier,EndGesture ending=EndGesture::Commit,const std::function<void()>& preview={}) {
    const QPoint logicalBegin=fixture.Logical(begin).toPoint(),logicalEnd=fixture.Logical(end).toPoint();
    auto* window=fixture.Viewport->windowHandle();fixture.Viewport->setFocus();
    QTest::mouseMove(window,logicalBegin,5);QTest::mousePress(window,Qt::LeftButton,modifiers,logicalBegin);
    Check(fixture.Services.Scenes->HasTransformEdit(),"Actual gizmo press did not begin a SceneManager transform transaction");
    try {
        for(int step=1;step<=8;++step) {
            const QPoint point=logicalBegin+(logicalEnd-logicalBegin)*step/8;
            QMouseEvent move(QEvent::MouseMove,QPointF(point),QPointF(fixture.Viewport->mapToGlobal(point)),Qt::NoButton,Qt::LeftButton,modifiers);
            QCoreApplication::sendEvent(fixture.Viewport,&move);fixture.Tick();
        }
        if(preview)preview();
        if(ending==EndGesture::Escape)QTest::keyClick(window,Qt::Key_Escape);
        if(ending==EndGesture::FocusLoss){QFocusEvent loss(QEvent::FocusOut);QCoreApplication::sendEvent(fixture.Viewport,&loss);}
        QTest::mouseRelease(window,Qt::LeftButton,modifiers,logicalEnd);fixture.Settle();
    }catch(...) {
        QTest::keyClick(window,Qt::Key_Escape);QTest::mouseRelease(window,Qt::LeftButton,modifiers,logicalEnd);throw;
    }
    Check(!fixture.Services.Scenes->HasTransformEdit(),"Gizmo release/cancel retained a hot transform transaction");
}
static int RedPixels(const QImage& image,const QPointF& logical,QSize logicalSize) {
    const float sx=float(image.width())/logicalSize.width(),sy=float(image.height())/logicalSize.height();
    const QPoint center(qRound(logical.x()*sx),qRound(logical.y()*sy));int result=0;
    for(int y=qMax(0,center.y()-6);y<=qMin(image.height()-1,center.y()+6);++y)
        for(int x=qMax(0,center.x()-6);x<=qMin(image.width()-1,center.x()+6);++x) {
            const QColor color=image.pixelColor(x,y);if(color.red()>180&&color.red()>color.green()*1.5&&color.red()>color.blue()*1.5)++result;
        }
    return result;
}
static void CheckPickingAndOverlay(SceneInteractionFixture& fixture) {
    auto* scenes=fixture.Services.Scenes;auto* back=fixture.Mesh("Back",{0,0,-2});auto* front=fixture.Mesh("Front",{0,0,1});
    auto* miss=fixture.Mesh("AABB is not a triangle hit",{0,0,2},3);fixture.Settle();
    fixture.Select(nullptr);fixture.Click({-.3f,.25f,1});
    Check(fixture.Services.Selection->GetSceneObjectSelection().ObjectID==front->GetInstanceID(),"Scene click did not select the nearest actual triangle");
    CheckInspectorPosition(fixture,*front);
    auto* hierarchy=static_cast<IHierarchyUI*>(fixture.Services.Editor.Views["HierarchyUI"]);
    Check(hierarchy->GetSelectedObjectID()==front->GetInstanceID(),"Viewport pick did not synchronize the real Hierarchy");
    front->SetActive(false);fixture.Select(nullptr);fixture.Click({-.3f,.25f,1});
    Check(fixture.Services.Selection->GetSceneObjectSelection().ObjectID==back->GetInstanceID(),"Inactive foreground pick expected Back="+std::to_string(back->GetInstanceID())+" got="+std::to_string(fixture.Services.Selection->GetSceneObjectSelection().ObjectID));
    front->SetActive(true);front->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->SetVisible(false);fixture.Select(nullptr);fixture.Click({-.3f,.25f,1});
    Check(fixture.Services.Selection->GetSceneObjectSelection().ObjectID==back->GetInstanceID(),"Invisible foreground mesh intercepted selection");
    front->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->SetVisible(true);
    auto* clipped=fixture.Mesh("Before near clip",{0,0,7.99f});fixture.Select(nullptr);fixture.Click({-.3f,.25f,1});
    Check(fixture.Services.Selection->GetSceneObjectSelection().ObjectID==front->GetInstanceID(),"Geometry before the camera near plane intercepted a visible mesh pick");
    clipped->SetActive(false);const auto camera=fixture.SceneView->GetCameraState();const auto pickPixel=fixture.Project({-.3f,.25f,1});
    auto farCamera=camera;farCamera.FarPlane=5;Check(fixture.SceneView->SetCameraState(farCamera),"Cannot shorten Scene camera far plane");fixture.Select(nullptr);fixture.Settle();fixture.ClickPixel(pickPixel);
    Check(fixture.Services.Selection->GetSceneObjectSelection().ObjectID==0,"Geometry beyond the camera far plane intercepted selection");
    Check(fixture.SceneView->SetCameraState(camera),"Cannot restore Scene camera far plane");
    miss->SetActive(false);fixture.Select(nullptr);fixture.Settle();
    const auto before=fixture.Capture();
    fixture.Select(back);fixture.Key(Qt::Key_W);SceneGizmoSettings settings;auto gizmo=ConfigureGizmo(fixture,*back,settings);
    const auto sample=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.6f));
    QTest::mouseMove(fixture.Viewport->windowHandle(),QPoint(5,5));fixture.Settle(60);
    const auto after=fixture.Capture();
    Check(after.save("/tmp/gamesprout-scene-gizmo-native.png"),"Cannot save native Scene editing screenshot");
    const auto logical=fixture.Logical(sample);const int beforeRed=RedPixels(before,logical,fixture.Viewport->size()),afterRed=RedPixels(after,logical,fixture.Viewport->size());
    std::cout<<"Native gizmo red pixels behind foreground triangle before="<<beforeRed<<" after="<<afterRed<<std::endl;
    Check(afterRed>=2&&afterRed>beforeRed,"Real Metal output did not draw the selected gizmo above occluding scene geometry");
    const auto backBefore=ReadTransform(*back),frontBefore=ReadTransform(*front);
    const auto hotBegin=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.6f));
    Check(gizmo.Hover(hotBegin)==SceneGizmoHandle::X,"Overlapped handle test did not target X shaft");
    const auto hotEnd=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.6f+.4f));
    DragGizmo(fixture,hotBegin,hotEnd,Qt::NoModifier,EndGesture::Escape,[&]{CheckInspectorPosition(fixture,*back);});
    Check(fixture.Services.Selection->GetSceneObjectSelection().ObjectID==back->GetInstanceID()&&CloseTransform(ReadTransform(*back),backBefore)&&CloseTransform(ReadTransform(*front),frontBefore)&&!scenes->CanUndo(),
          "Visible gizmo did not take mouse ownership ahead of occluding mesh selection");
    back->SetActive(false);front->SetActive(false);fixture.Select(nullptr);fixture.Settle();
    Check(!scenes->HasTransformEdit(),"Picking or rendering created an edit transaction");
}
static void CheckCaptureInvalidation(SceneInteractionFixture& fixture) {
    auto* scenes=fixture.Services.Scenes;auto* scene=scenes->GetActiveScene();auto* object=fixture.Mesh("Cancel Lifecycle",{-1,0,-1});fixture.Select(object);fixture.Key(Qt::Key_W);
    SetMode(fixture,"spaceCombo",0);SetMode(fixture,"pivotCombo",0);const auto original=ReadTransform(*object);SceneGizmoSettings settings;
    const auto checkReleased=[&]{Check(!scenes->HasTransformEdit()&&QWidget::mouseGrabber()!=fixture.Viewport,"Invalidated editing retained transform state or native mouse capture");};
    const auto resized=fixture.Host->size();auto gizmo=ConfigureGizmo(fixture,*object,settings);auto begin=AxisPixel(fixture,gizmo,0);
    auto end=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.75f+.5f));
    DragGizmo(fixture,begin,end,Qt::NoModifier,EndGesture::Commit,[&]{
        Check(QWidget::mouseGrabber()==fixture.Viewport,"Native gizmo did not acquire mouse capture");
        fixture.Host->resize(resized+QSize(60,30));fixture.Settle();checkReleased();
        Check(CloseTransform(ReadTransform(*object),original),"Resize committed or lost the original transform");
    });
    fixture.Host->resize(resized);fixture.Settle();fixture.Select(object);
    gizmo=ConfigureGizmo(fixture,*object,settings);begin=AxisPixel(fixture,gizmo,0);end=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.75f+.5f));
    DragGizmo(fixture,begin,end,Qt::NoModifier,EndGesture::Commit,[&]{
        fixture.Select(scene->GetMainCamera()->GetGameObject());checkReleased();
        Check(CloseTransform(ReadTransform(*object),original),"Switching selection committed a pending gizmo drag");
    });
    fixture.Select(object);gizmo=ConfigureGizmo(fixture,*object,settings);begin=AxisPixel(fixture,gizmo,0);end=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.75f+.5f));
    const auto objectID=object->GetInstanceID();
    DragGizmo(fixture,begin,end,Qt::NoModifier,EndGesture::Commit,[&]{
        Check(scene->DestroyGameObject(object),"Cannot delete active native gizmo target");fixture.Settle();checkReleased();
        Check(!scenes->GetRuntime()->QueryGameObject(objectID),"Invalidated gizmo target survived deletion");
    });
    Check(!scenes->CanUndo(),"Cancelled resize/selection/deletion gesture appended Undo");fixture.Select(nullptr);
}
static void CheckTransformsAndPersistence(SceneInteractionFixture& fixture) {
    auto* scenes=fixture.Services.Scenes;auto* scene=scenes->GetActiveScene();auto* main=scene->GetMainCamera();
    const auto mainWorld=main->GetGameObject()->GetTransform()->GetWorldMatrix();
    auto* parent=scene->CreateGameObject("Interactive Parent");parent->GetTransform()->SetLocalPosition({-.5f,0,-1});parent->GetTransform()->SetLocalScale({2,1,3});
    Check(parent->GetTransform()->SetLocalRotation(AxisRotation({0,1,0},25)),"Cannot rotate integration parent");
    auto* object=fixture.Mesh("Editable Mesh",{},2,parent);object->GetTransform()->SetLocalScale({-1,1,1});fixture.Select(object);fixture.Settle();
    const auto nodeCount=SceneObjectCount(scene);std::string error;
    Check(scenes->SaveSceneAs("Scenes/Interaction.scene",error),error);
    const auto initial=ReadTransform(*object);const auto initialWorld=WorldPoint(object->GetTransform()->GetWorldMatrix());
    for(const auto& setting:std::vector<std::pair<const char*,double>>{{"moveSnapSpin",1},{"rotateSnapSpin",15},{"scaleSnapSpin",.1}}) {
        auto* spin=fixture.SceneDock->findChild<QWidget*>(setting.first);Check(spin&&spin->setProperty("value",setting.second),std::string("Missing writable snap setting: ")+setting.first);
    }
    auto* snap=fixture.SceneDock->findChild<QCheckBox*>("snapCheck");Check(snap,"Scene snap control missing");snap->setChecked(false);
    fixture.Key(Qt::Key_W);CheckTool(fixture,"moveToolButton");SetMode(fixture,"spaceCombo",0);SetMode(fixture,"pivotCombo",0);
    SceneGizmoSettings settings;auto gizmo=ConfigureGizmo(fixture,*object,settings);
    const auto begin=AxisPixel(fixture,gizmo,0);const auto pivot=gizmo.GetPivotWorld();const float length=gizmo.GetHandleWorldSize();
    const auto end=fixture.Project(pivot+gizmo.GetAxis(0)*(length*.75f+1.37f));
    DragGizmo(fixture,begin,end,Qt::ControlModifier,EndGesture::Commit,[&]{CheckInspectorPosition(fixture,*object);Check(scenes->HasUnsavedChanges(),"Preview did not update scene dirty state");});
    const auto moved=ReadTransform(*object);
    Check(CloseVector(WorldPoint(object->GetTransform()->GetWorldMatrix())-initialWorld,{1,0,0},.035f),"World X drag/snapping ignored parent inverse or Ctrl increment");
    Check(CloseVector(moved.Scale,initial.Scale)&&CloseRotation(moved.Rotation,initial.Rotation),"Move changed rotation or scale");
    Check(scenes->Undo(error)&&CloseTransform(ReadTransform(*object),initial)&&!scenes->CanUndo()&&scenes->CanRedo(),"Eight native preview samples did not collapse to one Undo");fixture.Settle();

    // Cancel keeps the existing Redo and rolls back both Runtime and Inspector.
    gizmo=ConfigureGizmo(fixture,*object,settings);const auto cancelBegin=AxisPixel(fixture,gizmo,0);
    const auto cancelEnd=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*.75f+.65f));
    DragGizmo(fixture,cancelBegin,cancelEnd,Qt::NoModifier,EndGesture::Escape,[&]{CheckInspectorPosition(fixture,*object);});
    Check(CloseTransform(ReadTransform(*object),initial)&&scenes->CanRedo()&&!scenes->CanUndo(),"Escape committed a gizmo preview or destroyed Redo");CheckInspectorPosition(fixture,*object);
    DragGizmo(fixture,cancelBegin,cancelEnd,Qt::NoModifier,EndGesture::FocusLoss);
    Check(CloseTransform(ReadTransform(*object),initial)&&scenes->CanRedo(),"Focus loss failed to cancel a gizmo preview");
    DragGizmo(fixture,cancelBegin,cancelBegin);
    Check(CloseTransform(ReadTransform(*object),initial)&&scenes->CanRedo()&&!scenes->CanUndo(),"A zero-motion native gizmo click appended Undo or truncated Redo");
    for(const auto tool:{SceneGizmoTool::Rotate,SceneGizmoTool::Scale}) {
        fixture.Key(tool==SceneGizmoTool::Rotate?Qt::Key_E:Qt::Key_R);settings.Tool=tool;
        gizmo=ConfigureGizmo(fixture,*object,settings);
        const auto noMotion=tool==SceneGizmoTool::Rotate?fixture.Project(RingPoint(fixture,gizmo,2)):AxisPixel(fixture,gizmo,0);
        DragGizmo(fixture,noMotion,noMotion);
        Check(CloseTransform(ReadTransform(*object),initial)&&scenes->CanRedo()&&!scenes->CanUndo()&&!scenes->HasUnsavedChanges(),
              "A zero-motion world rotate/scale click changed mirrored local TRS or history");
    }
    Check(scenes->Redo(error)&&CloseTransform(ReadTransform(*object),moved),error);fixture.Settle();

    // Local rotation and scaling remain representable under a rotated, nonuniform parent.
    SetMode(fixture,"spaceCombo",1);SetMode(fixture,"pivotCombo",1);fixture.Key(Qt::Key_E);
    CheckTool(fixture,"rotateToolButton");
    QLineEdit* readonlyResource=nullptr;
    for(auto* field:fixture.InspectorDock->findChildren<QLineEdit*>("resourceNameEdit"))
        if(field->isVisible()&&field->isReadOnly()){readonlyResource=field;break;}
    Check(readonlyResource,"Inspector resource field missing");
    readonlyResource->setFocus();QTest::keyClick(readonlyResource,Qt::Key_W);CheckTool(fixture,"rotateToolButton");fixture.Viewport->setFocus();
    settings.Tool=SceneGizmoTool::Rotate;settings.Space=SceneGizmoSpace::Local;settings.Pivot=SceneGizmoPivot::Center;
    gizmo=ConfigureGizmo(fixture,*object,settings);const auto ring=RingPoint(fixture,gizmo,2),center=gizmo.GetPivotWorld();
    const auto rotation=GameSproutRuntime::RuntimeMath::ComposeTRS({},AxisRotation(gizmo.GetAxis(2),37),{1,1,1});
    const auto rotatedDirection=rotation*GameSproutRuntime::Vector4{ring.X-center.X,ring.Y-center.Y,ring.Z-center.Z,0};
    const auto beforeRotation=ReadTransform(*object);GameSproutRuntime::Bounds boundsBefore;Check(fixture.Viewport->Renderer()->GetWorldBounds(scene,object->GetInstanceID(),boundsBefore),"No bounds for Center rotation");
    DragGizmo(fixture,fixture.Project(ring),fixture.Project(center+GameSproutRuntime::Vector3{rotatedDirection.X,rotatedDirection.Y,rotatedDirection.Z}),Qt::MetaModifier);
    const auto rotated=ReadTransform(*object);GameSproutRuntime::Bounds boundsAfter;Check(fixture.Viewport->Renderer()->GetWorldBounds(scene,object->GetInstanceID(),boundsAfter),"No rotated bounds");
    Check(!CloseRotation(rotated.Rotation,beforeRotation.Rotation)&&CloseVector(boundsBefore.Center,boundsAfter.Center,.015f),"Local Center rotation did not rotate or moved the geometry center");
    const float angle=RotationDifferenceDegrees(beforeRotation.Rotation,rotated.Rotation);
    Check(std::fabs(angle/15-std::round(angle/15))<.015f,"Cmd rotation did not snap to its 15-degree increment");
    Check(scenes->Undo(error)&&CloseTransform(ReadTransform(*object),beforeRotation),"Rotation did not undo as one operation");
    Check(scenes->Redo(error)&&CloseTransform(ReadTransform(*object),rotated),"Rotation Redo failed");fixture.Settle();
    fixture.Key(Qt::Key_R);CheckTool(fixture,"scaleToolButton");settings.Tool=SceneGizmoTool::Scale;gizmo=ConfigureGizmo(fixture,*object,settings);
    const auto scaleBegin=AxisPixel(fixture,gizmo,0);const auto scaleEnd=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*1.12f));
    const auto beforeScale=ReadTransform(*object);Check(fixture.Viewport->Renderer()->GetWorldBounds(scene,object->GetInstanceID(),boundsBefore),"No bounds for Center scaling");
    DragGizmo(fixture,scaleBegin,scaleEnd,Qt::ControlModifier);const auto scaled=ReadTransform(*object);
    Check(fixture.Viewport->Renderer()->GetWorldBounds(scene,object->GetInstanceID(),boundsAfter),"No scaled bounds");
    Check(std::fabs(scaled.Scale.X/beforeScale.Scale.X-1.4f)<.012f&&std::fabs(scaled.Scale.Y-beforeScale.Scale.Y)<.001f&&CloseVector(boundsBefore.Center,boundsAfter.Center,.015f),"Local Center scale did not preserve negative sign, axis factor/snap or geometry center");
    Check(scenes->Undo(error)&&CloseTransform(ReadTransform(*object),beforeScale)&&scenes->Redo(error)&&CloseTransform(ReadTransform(*object),scaled),"Scale did not form a single reversible edit");fixture.Settle();

    SetMode(fixture,"spaceCombo",0);SetMode(fixture,"pivotCombo",0);settings.Space=SceneGizmoSpace::World;settings.Pivot=SceneGizmoPivot::Pivot;
    gizmo=ConfigureGizmo(fixture,*object,settings);const auto rejectedBegin=AxisPixel(fixture,gizmo,0);
    const auto rejectedEnd=fixture.Project(gizmo.GetPivotWorld()+gizmo.GetAxis(0)*(gizmo.GetHandleWorldSize()*1.15f));
    DragGizmo(fixture,rejectedBegin,rejectedEnd);
    Check(CloseTransform(ReadTransform(*object),scaled),"World scale silently approximated a sheared transform as TRS");
    Check(scenes->Undo(error)&&CloseTransform(ReadTransform(*object),beforeScale)&&scenes->Redo(error)&&CloseTransform(ReadTransform(*object),scaled),"Rejected world shear appended an extra Undo operation");fixture.Settle();

    // Q switches ownership to navigation: a hand drag moves only the temporary camera.
    fixture.Key(Qt::Key_Q);CheckTool(fixture,"viewToolButton");const auto camera=fixture.SceneView->GetCameraState();const auto beforeHand=ReadTransform(*object);
    auto* window=fixture.Viewport->windowHandle();QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,QPoint(25,25));
    QMouseEvent hand(QEvent::MouseMove,QPointF(65,50),QPointF(fixture.Viewport->mapToGlobal(QPoint(65,50))),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(fixture.Viewport,&hand);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,QPoint(65,50));fixture.Settle();
    Check(!CloseVector(camera.Position,fixture.SceneView->GetCameraState().Position)&&CloseTransform(ReadTransform(*object),beforeHand)&&!scenes->HasTransformEdit(),"Q hand tool edited the node or failed to navigate");
    Check(SceneObjectCount(scene)==nodeCount&&scene->GetMainCamera()==main,"Scene tools created hidden runtime nodes or changed Game MainCamera");
    const auto mainAfter=main->GetGameObject()->GetTransform()->GetWorldMatrix();for(int index=0;index<16;++index)Check(mainAfter.Data[index]==mainWorld.Data[index],"Scene tools modified Game camera transform");
    Check(scenes->SaveScene(error),error);const auto saved=fixture.Services.ReadScene("Interaction.scene");
    Check(scenes->OpenScene("Scenes/Interaction.scene",error),error);fixture.Settle();
    auto* restoredParent=scenes->GetActiveScene()->FindChild("Interactive Parent");auto* restored=restoredParent?restoredParent->FindChild("Editable Mesh"):nullptr;
    Check(restored&&CloseTransform(ReadTransform(*restored),scaled),"Saving/reopening .scene lost gizmo edits");fixture.Select(restored);CheckInspectorPosition(fixture,*restored);
    Check(scenes->SaveScene(error)&&fixture.Services.ReadScene("Interaction.scene")==saved&&!scenes->HasUnsavedChanges(),"Gizmo save/reopen changed FileIDs or serialized temporary handles/camera");
}
static void CheckNarrowToolbar(SceneInteractionFixture& fixture) {
    auto* toolbar=fixture.SceneDock->findChild<QScrollArea*>("toolScrollArea");Check(toolbar,"Scene tool scroll area missing");
    Check(fixture.SceneDock->widget()->minimumSizeHint().width()<=360,"Scene toolbar imposed its full tool content width on the dock");
    fixture.Host->resize(940,760);fixture.Settle();
    Check(fixture.IDE->ResizeDockers({fixture.IDE->QueryDocker("Hierarchy"),fixture.IDE->QueryDocker("Scene"),fixture.IDE->QueryDocker("Inspector")},{140,330,470},Qt::Horizontal),"Cannot resize Scene to a narrow pane");
    fixture.Pump([&]{GameSproutRuntime::RenderedCamera frame;return fixture.Viewport->GetRenderedCamera(frame)&&frame.Width==qRound(fixture.Viewport->width()*fixture.Viewport->devicePixelRatioF());});
    std::cout<<"Narrow Scene host="<<fixture.Host->width()<<" dock="<<fixture.SceneDock->width()<<" toolbar="<<toolbar->width()<<"x"<<toolbar->height()<<" content="<<toolbar->widget()->width()<<" minHint="<<toolbar->widget()->minimumSizeHint().width()<<" scrollRange="<<toolbar->horizontalScrollBar()->minimum()<<".."<<toolbar->horizontalScrollBar()->maximum()<<std::endl;
    Check(fixture.SceneDock->width()<450&&toolbar->horizontalScrollBar()->maximum()>0&&toolbar->height()<=100,
          "Narrow Scene pane could not horizontally scroll its fixed-height tools");
    Check(fixture.Capture().save("/tmp/gamesprout-scene-editing-narrow.png"),"Cannot capture narrow Scene editing tools");
    const QPoint origin=fixture.SceneDock->mapTo(fixture.Host,QPoint());
    Check(fixture.Host->screen()->grabWindow(fixture.Host->winId(),origin.x(),origin.y(),fixture.SceneDock->width(),fixture.SceneDock->height()).save("/tmp/gamesprout-scene-toolbar-narrow.png"),"Cannot capture narrow Scene toolbar with native viewport");
    Check(!fixture.Services.Scenes->HasUnsavedChanges(),"Scene toolbar resize modified the saved scene");
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);if(SceneViewport::IsHeadless()){std::cout<<"SKIP native editing requires Cocoa/Windows/X11\n";return 0;}
    GRHI::GRHIApi* rhi=nullptr;
    try {
        rhi=ExportGRHIApi(TEST_RUNTIME_ROOT);Check(rhi,"Cannot load GRHI for real Scene interaction");
        {SceneInteractionFixture fixture(rhi);fixture.Initialize();CheckPickingAndOverlay(fixture);CheckCaptureInvalidation(fixture);CheckTransformsAndPersistence(fixture);CheckNarrowToolbar(fixture);}
        delete rhi;rhi=nullptr;std::cout<<"PASS real Qt/Metal pick, overlay, gizmo transactions, Inspector and scene persistence\n";return 0;
    }catch(const std::exception& exception){delete rhi;std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}

#include "SceneViewport.h"
#include "SceneViewportEditing.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QHideEvent>

namespace {
QPointF Position(QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    return event->position();
#else
    return event->localPos();
#endif
}
GameSproutRuntime::Vector2 Pixel(QMouseEvent* event){const auto p=Position(event);return {float(p.x()),float(p.y())};}
bool Snap(Qt::KeyboardModifiers modifiers){return modifiers&(Qt::ControlModifier|Qt::MetaModifier);}
bool MovementKey(int key){return key==Qt::Key_W||key==Qt::Key_A||key==Qt::Key_S||key==Qt::Key_D||key==Qt::Key_Q||key==Qt::Key_E||key==Qt::Key_Shift;}
}
void SceneViewport::ReleaseEditingCapture() {
    if(_Drag!=DragMode::None)return;
    if(QWidget::mouseGrabber()==this)releaseMouse();
    unsetCursor();
}
void SceneViewport::ClearInput() {
    _Keys.clear();_Drag=DragMode::None;_DragButton=Qt::NoButton;
    // 先结束事务，再释放捕获；UngrabMouse 可能同步重新进入此函数。
    _Editing->Cancel();
    if(QWidget::mouseGrabber()==this)releaseMouse();
    unsetCursor();
}
void SceneViewport::AdvanceInput(float dt) {
    if(!isVisible()||!hasFocus()){ClearInput();return;}
    if(_Drag!=DragMode::Look)return;
    _Camera.Move((_Keys.contains(Qt::Key_W)?1:0)-(_Keys.contains(Qt::Key_S)?1:0),
        (_Keys.contains(Qt::Key_D)?1:0)-(_Keys.contains(Qt::Key_A)?1:0),
        (_Keys.contains(Qt::Key_E)?1:0)-(_Keys.contains(Qt::Key_Q)?1:0),_Keys.contains(Qt::Key_Shift),dt);
}
void SceneViewport::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    DragMode mode=DragMode::None;
    if(event->button()==Qt::RightButton && !(event->modifiers()&Qt::AltModifier))mode=DragMode::Look;
    else if(event->button()==Qt::MiddleButton)mode=DragMode::Pan;
    else if(event->button()==Qt::LeftButton && (event->modifiers()&Qt::AltModifier))mode=DragMode::Orbit;
    else if(event->button()==Qt::LeftButton && _Editing->Tool()==SceneEditingTool::View)mode=DragMode::Pan;
    if(mode!=DragMode::None) {
        ClearInput();_Drag=mode;_DragButton=event->button();_LastMouse=Position(event);
        if(!IsHeadless())grabMouse();
        setCursor(mode==DragMode::Look?Qt::BlankCursor:Qt::ClosedHandCursor);event->accept();return;
    }
    if(_Drag==DragMode::None&&event->button()==Qt::LeftButton&&_Editing->Press(Pixel(event))) {
        if(_Editing->IsDragging()) {if(!IsHeadless())grabMouse();setCursor(Qt::SizeAllCursor);}
        event->accept();return;
    }
    QWidget::mousePressEvent(event);
}
void SceneViewport::mouseMoveEvent(QMouseEvent* event) {
    if(_Drag==DragMode::None) {
        if(_Editing->Move(Pixel(event),Snap(event->modifiers()))){event->accept();return;}
        QWidget::mouseMoveEvent(event);return;
    }
    const QPointF point=Position(event),delta=point-_LastMouse;_LastMouse=point;
    if(_Drag==DragMode::Pan)_Camera.Pan(static_cast<float>(delta.x()),static_cast<float>(delta.y()),static_cast<float>(height()));
    else _Camera.Look(static_cast<float>(delta.x()),static_cast<float>(delta.y()),_Drag==DragMode::Orbit);
    event->accept();
}
void SceneViewport::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()==Qt::LeftButton&&_Editing->Release(Pixel(event),Snap(event->modifiers()))) {
        ReleaseEditingCapture();event->accept();return;
    }
    if(event->button()==Qt::LeftButton&&_Drag==DragMode::None)ReleaseEditingCapture();
    if(event->button()==_DragButton){ClearInput();event->accept();return;}
    QWidget::mouseReleaseEvent(event);
}
void SceneViewport::wheelEvent(QWheelEvent* event) {
    const float steps=event->angleDelta().y()!=0?static_cast<float>(event->angleDelta().y())/120.0f:static_cast<float>(event->pixelDelta().y())/40.0f;
    if(steps==0){event->ignore();return;}
    _Editing->Cancel();_Camera.Dolly(steps);event->accept();
}
void SceneViewport::keyPressEvent(QKeyEvent* event) {
    if(event->key()==Qt::Key_Escape&&(_Drag!=DragMode::None||_Editing->IsDragging())){ClearInput();event->accept();return;}
    if(_Drag==DragMode::None&&event->modifiers()==Qt::NoModifier&&
        (event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)) {
        if(!event->isAutoRepeat())_Editing->DeleteSelection();event->accept();return;
    }
    if(_Drag==DragMode::Look && MovementKey(event->key()) && !(event->modifiers()&(Qt::ControlModifier|Qt::MetaModifier|Qt::AltModifier))) {
        _Keys.insert(event->key());event->accept();return;
    }
    if(event->key()==Qt::Key_F && event->modifiers()==Qt::NoModifier) {
        if(!event->isAutoRepeat()){ClearInput();if(FocusRequested&&!FocusRequested())SetStatus("请先选择当前场景中的节点。");}
        event->accept();return;
    }
    if(_Drag==DragMode::None&&event->modifiers()==Qt::NoModifier) {
        SceneEditingTool tool;
        switch(event->key()) {
        case Qt::Key_Q:tool=SceneEditingTool::View;break;
        case Qt::Key_W:tool=SceneEditingTool::Move;break;
        case Qt::Key_E:tool=SceneEditingTool::Rotate;break;
        case Qt::Key_R:tool=SceneEditingTool::Scale;break;
        default:QWidget::keyPressEvent(event);return;
        }
        if(!event->isAutoRepeat()){ClearInput();_Editing->SetTool(tool);}event->accept();return;
    }
    QWidget::keyPressEvent(event);
}
void SceneViewport::keyReleaseEvent(QKeyEvent* event) {
    if(_Keys.contains(event->key())){if(!event->isAutoRepeat())_Keys.remove(event->key());event->accept();return;}
    QWidget::keyReleaseEvent(event);
}
void SceneViewport::focusOutEvent(QFocusEvent* event){ClearInput();QWidget::focusOutEvent(event);}
void SceneViewport::hideEvent(QHideEvent* event){ClearInput();QWidget::hideEvent(event);}

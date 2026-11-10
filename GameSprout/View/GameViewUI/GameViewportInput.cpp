#include "GameViewport.h"
#include "GameSproutRuntimeApi.h"
#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace {
QPointF MousePosition(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    return event->position();
#else
    return event->localPos();
#endif
}
GameSproutRuntime::InputKey Key(int key) {
    using GameSproutRuntime::InputKey;
    if(key>=Qt::Key_A&&key<=Qt::Key_Z)return static_cast<InputKey>(static_cast<int>(InputKey::A)+key-Qt::Key_A);
    if(key>=Qt::Key_0&&key<=Qt::Key_9)return static_cast<InputKey>(static_cast<int>(InputKey::Digit0)+key-Qt::Key_0);
    if(key>=Qt::Key_F1&&key<=Qt::Key_F12)return static_cast<InputKey>(static_cast<int>(InputKey::F1)+key-Qt::Key_F1);
    switch(key) {
    case Qt::Key_Escape:return InputKey::Escape;
    case Qt::Key_Return:case Qt::Key_Enter:return InputKey::Enter;
    case Qt::Key_Space:return InputKey::Space;
    case Qt::Key_Tab:return InputKey::Tab;
    case Qt::Key_Backspace:return InputKey::Backspace;
    case Qt::Key_Delete:return InputKey::Delete;
    case Qt::Key_Insert:return InputKey::Insert;
    case Qt::Key_Home:return InputKey::Home;
    case Qt::Key_End:return InputKey::End;
    case Qt::Key_PageUp:return InputKey::PageUp;
    case Qt::Key_PageDown:return InputKey::PageDown;
    case Qt::Key_Left:return InputKey::Left;
    case Qt::Key_Right:return InputKey::Right;
    case Qt::Key_Up:return InputKey::Up;
    case Qt::Key_Down:return InputKey::Down;
    case Qt::Key_Shift:return InputKey::Shift;
    case Qt::Key_Control:return InputKey::Control;
    case Qt::Key_Alt:case Qt::Key_AltGr:return InputKey::Alt;
    case Qt::Key_Meta:return InputKey::Super;
    case Qt::Key_CapsLock:return InputKey::CapsLock;
    default:return InputKey::None;
    }
}
bool Button(Qt::MouseButton button,GameSproutRuntime::InputMouseButton& result) {
    switch(button) {
    case Qt::LeftButton:result=GameSproutRuntime::InputMouseButton::Left;return true;
    case Qt::RightButton:result=GameSproutRuntime::InputMouseButton::Right;return true;
    case Qt::MiddleButton:result=GameSproutRuntime::InputMouseButton::Middle;return true;
    case Qt::BackButton:result=GameSproutRuntime::InputMouseButton::X1;return true;
    case Qt::ForwardButton:result=GameSproutRuntime::InputMouseButton::X2;return true;
    default:return false;
    }
}
GameSproutRuntime::RuntimeInputEvent PointerEvent(const QPointF& point) {
    GameSproutRuntime::RuntimeInputEvent result;result.Type=GameSproutRuntime::InputEventType::MouseMove;
    result.X=static_cast<float>(point.x());result.Y=static_cast<float>(point.y());return result;
}
}

void GameViewport::SetGameplayInputEnabled(bool enabled) {
    if(_GameplayInputEnabled==enabled)return;
    _GameplayInputEnabled=enabled;
    if(!enabled){ReleaseGameInput();clearFocus();}
    // Starting/resuming never steals keyboard focus from another editor panel.
}
void GameViewport::ReleaseGameInput() {
    if(_InputCaptured&&_Runtime) {
        GameSproutRuntime::RuntimeInputEvent event;event.Type=GameSproutRuntime::InputEventType::FocusLost;
        _Runtime->SubmitInputEvent(event);
    }
    _InputCaptured=false;
}
void GameViewport::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if(!_GameplayInputEnabled||!_Runtime)return;
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::FocusGained;
    _InputCaptured=_Runtime->SubmitInputEvent(input);
}
void GameViewport::focusOutEvent(QFocusEvent* event) {ReleaseGameInput();QWidget::focusOutEvent(event);}
void GameViewport::hideEvent(QHideEvent* event) {
    ReleaseGameInput();
    if(_Runtime)_Runtime->SetUIInputViewport(nullptr,{});
    QWidget::hideEvent(event);
}
void GameViewport::keyPressEvent(QKeyEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::keyPressEvent(event);return;}
    if(event->key()==Qt::Key_Escape){ReleaseGameInput();clearFocus();event->accept();return;}
    if(event->isAutoRepeat()){event->accept();return;}
    const auto key=Key(event->key());
    if(key==GameSproutRuntime::InputKey::None){QWidget::keyPressEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::KeyDown;input.Key=key;
    _Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::keyReleaseEvent(QKeyEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::keyReleaseEvent(event);return;}
    if(event->isAutoRepeat()){event->accept();return;}
    const auto key=Key(event->key());
    if(key==GameSproutRuntime::InputKey::None){QWidget::keyReleaseEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::KeyUp;input.Key=key;
    _Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::mousePressEvent(QMouseEvent* event) {
    if(!_GameplayInputEnabled||!_Runtime){QWidget::mousePressEvent(event);return;}
    setFocus(Qt::MouseFocusReason);
    // The widget may already own Qt focus from Edit mode; in that case Qt
    // emits no new FocusIn. An explicit game click still acquires gameplay input.
    if(!_InputCaptured&&hasFocus()) {
        GameSproutRuntime::RuntimeInputEvent focus;focus.Type=GameSproutRuntime::InputEventType::FocusGained;
        _InputCaptured=_Runtime->SubmitInputEvent(focus);
    }
    if(!_InputCaptured){QWidget::mousePressEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::MouseButtonDown;
    if(!Button(event->button(),input.Button)){QWidget::mousePressEvent(event);return;}
    _Runtime->SubmitInputEvent(PointerEvent(MousePosition(event)));_Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::mouseReleaseEvent(QMouseEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::mouseReleaseEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::MouseButtonUp;
    if(!Button(event->button(),input.Button)){QWidget::mouseReleaseEvent(event);return;}
    _Runtime->SubmitInputEvent(PointerEvent(MousePosition(event)));_Runtime->SubmitInputEvent(input);event->accept();
}
void GameViewport::mouseMoveEvent(QMouseEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::mouseMoveEvent(event);return;}
    _Runtime->SubmitInputEvent(PointerEvent(MousePosition(event)));event->accept();
}
void GameViewport::wheelEvent(QWheelEvent* event) {
    if(!_InputCaptured||!_GameplayInputEnabled||!_Runtime){QWidget::wheelEvent(event);return;}
    GameSproutRuntime::RuntimeInputEvent input;input.Type=GameSproutRuntime::InputEventType::MouseWheel;
    input.WheelX=event->angleDelta().x()/120.f;input.WheelY=event->angleDelta().y()/120.f;
    _Runtime->SubmitInputEvent(input);event->accept();
}

bool GameViewport::focusNextPrevChild(bool next) {
    return _InputCaptured&&_GameplayInputEnabled?false:QWidget::focusNextPrevChild(next);
}

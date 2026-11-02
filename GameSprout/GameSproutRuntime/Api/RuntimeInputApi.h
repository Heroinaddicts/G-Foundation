#pragma once
#include "Math/Vector2.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace GameSproutRuntime {
// Gameplay key identifiers, independent of Qt/SDL/platform virtual-key values.
// Text composition/IME is a separate service; these events do not represent text.
enum class InputKey : std::uint16_t {
    None,A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    Digit0,Digit1,Digit2,Digit3,Digit4,Digit5,Digit6,Digit7,Digit8,Digit9,
    Escape,Enter,Space,Tab,Backspace,Delete,Insert,Home,End,PageUp,PageDown,
    Left,Right,Up,Down,Shift,Control,Alt,Super,CapsLock,
    F1,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,Count
};
enum class InputMouseButton : std::uint8_t { Left,Right,Middle,X1,X2,Count };
enum class InputEventType : std::uint8_t {
    FocusGained,FocusLost,KeyDown,KeyUp,MouseMove,MouseButtonDown,MouseButtonUp,MouseWheel
};
struct RuntimeInputEvent {
    InputEventType Type=InputEventType::FocusLost;
    InputKey Key=InputKey::None;
    InputMouseButton Button=InputMouseButton::Left;
    // Window-local logical pixels: top-left origin, X right, Y down.
    float X=0,Y=0;
    // Wheel steps; one conventional wheel detent is 1, positive Y scrolls up.
    float WheelX=0,WheelY=0;
};
struct InputButtonState { bool Held=false,Pressed=false,Released=false; };
struct RuntimeInputState {
    bool Focused=false,HasMousePosition=false;
    Vector2 MousePosition,MouseDelta,WheelDelta;
    std::array<InputButtonState,static_cast<std::size_t>(InputKey::Count)> Keys{};
    std::array<InputButtonState,static_cast<std::size_t>(InputMouseButton::Count)> MouseButtons{};
    InputButtonState GetKey(InputKey key) const {
        const auto index=static_cast<std::size_t>(key);
        return index<Keys.size()?Keys[index]:InputButtonState{};
    }
    InputButtonState GetMouseButton(InputMouseButton button) const {
        const auto index=static_cast<std::size_t>(button);
        return index<MouseButtons.size()?MouseButtons[index]:InputButtonState{};
    }
};
}

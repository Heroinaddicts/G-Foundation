#include "GameSproutRuntime.h"
#include <cmath>

namespace GameSproutRuntime {
namespace {
void Press(InputButtonState& state){if(!state.Held)state.Pressed=true;state.Held=true;}
void Release(InputButtonState& state){if(state.Held)state.Released=true;state.Held=false;}
}
void Runtime::ResetInput(){_Input={};ResetUIPointer();}
void Runtime::SetInputEnabled(bool enabled){
    _InputEnabled=enabled&&_ScriptExecutionEnabled&&!_ShuttingDown;
    ResetInput();
}
void Runtime::CompleteInputFrame(){
    for(auto& key:_Input.Keys){key.Pressed=false;key.Released=false;}
    for(auto& button:_Input.MouseButtons){button.Pressed=false;button.Released=false;}
    _Input.MouseDelta={};_Input.WheelDelta={};
}
bool Runtime::SubmitInputEvent(const RuntimeInputEvent& event){
    if(!_InputEnabled||!_ScriptExecutionEnabled||_ShuttingDown)return false;
    if(event.Type==InputEventType::FocusLost){
        ResetUIPointer();
        for(auto& key:_Input.Keys){key.Pressed=false;Release(key);}
        for(auto& button:_Input.MouseButtons){button.Pressed=false;Release(button);}
        _Input.Focused=false;_Input.HasMousePosition=false;_Input.MouseDelta={};_Input.WheelDelta={};return true;
    }
    if(event.Type==InputEventType::FocusGained){
        // Duplicate focus notifications must not discard a currently held key.
        if(!_Input.Focused){ResetInput();_Input.Focused=true;}
        return true;
    }
    if(!_Input.Focused)return false;
    switch(event.Type){
    case InputEventType::KeyDown:
    case InputEventType::KeyUp:{
        const auto index=static_cast<std::size_t>(event.Key);
        if(!index||index>=_Input.Keys.size())return false;
        if(event.Type==InputEventType::KeyDown)Press(_Input.Keys[index]);else Release(_Input.Keys[index]);
        return true;
    }
    case InputEventType::MouseButtonDown:
    case InputEventType::MouseButtonUp:{
        const auto index=static_cast<std::size_t>(event.Button);
        if(index>=_Input.MouseButtons.size())return false;
        const bool down=event.Type==InputEventType::MouseButtonDown;
        auto& state=_Input.MouseButtons[index];
        if(event.Button==InputMouseButton::Left&&state.Held!=down)QueueUIPointer(down);
        if(down)Press(state);else Release(state);
        return true;
    }
    case InputEventType::MouseMove:{
        if(!std::isfinite(event.X)||!std::isfinite(event.Y))return false;
        if(_Input.HasMousePosition){_Input.MouseDelta.X+=event.X-_Input.MousePosition.X;_Input.MouseDelta.Y+=event.Y-_Input.MousePosition.Y;}
        _Input.MousePosition={event.X,event.Y};_Input.HasMousePosition=true;return true;
    }
    case InputEventType::MouseWheel:
        if(!std::isfinite(event.WheelX)||!std::isfinite(event.WheelY))return false;
        _Input.WheelDelta.X+=event.WheelX;_Input.WheelDelta.Y+=event.WheelY;return true;
    default:return false;
    }
}
}

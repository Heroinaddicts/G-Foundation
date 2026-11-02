#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
GameSproutRuntime::RuntimeInputEvent Focus(GameSproutRuntime::InputEventType type){GameSproutRuntime::RuntimeInputEvent event;event.Type=type;return event;}
GameSproutRuntime::RuntimeInputEvent Key(GameSproutRuntime::InputEventType type,GameSproutRuntime::InputKey key){auto event=Focus(type);event.Key=key;return event;}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;auto* runtime=Export(argv[1]);
    try{
        Check(runtime,"Cannot load Runtime");std::string error;
        Check(!runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusGained)),"Edit mode accepted gameplay input");
        Check(runtime->BeginExecution(error),error.c_str());
        const auto down=Key(GameSproutRuntime::InputEventType::KeyDown,GameSproutRuntime::InputKey::W);
        Check(!runtime->SubmitInputEvent(down),"Unfocused gameplay accepted keys");
        Check(runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusGained)),"Cannot acquire gameplay input focus");
        Check(runtime->SubmitInputEvent(down),"Cannot press gameplay key");
        auto state=runtime->GetInputState();Check(state.GetKey(GameSproutRuntime::InputKey::W).Held&&state.GetKey(GameSproutRuntime::InputKey::W).Pressed,"Key press was not visible");
        runtime->CompleteInputFrame();Check(runtime->SubmitInputEvent(down),"Repeat down rejected");
        state=runtime->GetInputState();Check(state.GetKey(GameSproutRuntime::InputKey::W).Held&&!state.GetKey(GameSproutRuntime::InputKey::W).Pressed,"Repeated keydown retriggered Pressed");
        auto move=Focus(GameSproutRuntime::InputEventType::MouseMove);move.X=200;move.Y=300;Check(runtime->SubmitInputEvent(move),"Cannot seed mouse position");
        Check(runtime->GetInputState().MouseDelta.X==0&&runtime->GetInputState().MouseDelta.Y==0,"First focused mouse position produced a jump");
        move.X=210;move.Y=280;runtime->SubmitInputEvent(move);move.X=220;move.Y=290;runtime->SubmitInputEvent(move);
        auto wheel=Focus(GameSproutRuntime::InputEventType::MouseWheel);wheel.WheelX=.5f;wheel.WheelY=1;runtime->SubmitInputEvent(wheel);runtime->SubmitInputEvent(wheel);
        state=runtime->GetInputState();Check(state.MouseDelta.X==20&&state.MouseDelta.Y==-10&&state.WheelDelta.X==1&&state.WheelDelta.Y==2,"Mouse motion or wheel did not accumulate within the frame");
        auto mouse=Focus(GameSproutRuntime::InputEventType::MouseButtonDown);mouse.Button=GameSproutRuntime::InputMouseButton::Right;runtime->SubmitInputEvent(mouse);
        Check(runtime->GetInputState().GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Pressed,"Mouse button press was not visible");
        runtime->CompleteInputFrame();state=runtime->GetInputState();
        Check(state.MouseDelta.X==0&&state.WheelDelta.Y==0&&state.MousePosition.X==220&&state.GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Held&&!state.GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Pressed,"Frame completion lost held state or retained transient motion");
        Check(runtime->SubmitInputEvent(Key(GameSproutRuntime::InputEventType::KeyUp,GameSproutRuntime::InputKey::W)),"Cannot release key");
        Check(!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held&&runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Released,"Key release was not visible");
        runtime->SubmitInputEvent(down);runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusLost));state=runtime->GetInputState();
        Check(!state.Focused&&!state.GetKey(GameSproutRuntime::InputKey::W).Held&&state.GetKey(GameSproutRuntime::InputKey::W).Released&&!state.GetKey(GameSproutRuntime::InputKey::W).Pressed&&!state.GetMouseButton(GameSproutRuntime::InputMouseButton::Right).Held,"Focus loss left held keys/buttons or a stale press");
        runtime->SetInputEnabled(false);Check(!runtime->SubmitInputEvent(down)&&!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Released,"Pause retained transient input");
        runtime->SetInputEnabled(true);Check(!runtime->GetInputState().Focused&&!runtime->SubmitInputEvent(down),"Resume reused stale focus");
        runtime->SubmitInputEvent(Focus(GameSproutRuntime::InputEventType::FocusGained));
        move.X=std::numeric_limits<float>::quiet_NaN();Check(!runtime->SubmitInputEvent(move),"Nonfinite mouse input accepted");
        Check(!runtime->SubmitInputEvent(Key(GameSproutRuntime::InputEventType::KeyDown,GameSproutRuntime::InputKey::Count)),"Invalid gameplay key accepted");
        runtime->SubmitInputEvent(down);Check(runtime->EndExecution(error),error.c_str());
        Check(!runtime->GetInputState().Focused&&!runtime->GetInputState().GetKey(GameSproutRuntime::InputKey::W).Held&&!runtime->SubmitInputEvent(down),"Stop did not reset and disable gameplay input");
        Check(runtime->BeginExecution(error)&&!runtime->GetInputState().Focused,"New session inherited old input");runtime->EndExecution(error);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS gameplay input: mode/focus gating, key/button edges, held state, mouse/wheel accumulation, pause/resume and stop\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}

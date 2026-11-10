#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const char* error){if(!value)throw std::runtime_error(error);}
GameSproutRuntime::RuntimeInputEvent Focus(GameSproutRuntime::InputEventType type){GameSproutRuntime::RuntimeInputEvent event;event.Type=type;return event;}
GameSproutRuntime::RuntimeInputEvent Key(GameSproutRuntime::InputEventType type,GameSproutRuntime::InputKey key){auto event=Focus(type);event.Key=key;return event;}

void CheckCanvasInput(GameSproutRuntime::IGameSproutRuntime& runtime) {
    using namespace GameSproutRuntime;
    auto* scene = runtime.CreateScene("UI input");
    Check(scene, "Cannot create UI scene");
    auto* canvas = scene->CreateGameObject("Canvas", scene);
    Check(canvas && canvas->AddComponent<IRectTransformComponent>(), "Cannot create Canvas RectTransform");
    auto* canvasComponent = canvas->AddComponent<ICanvasComponent>();
    Check(canvasComponent && canvasComponent->SetReferenceResolution({800, 600}), "Cannot configure Canvas");
    auto* object = scene->CreateGameObject("Button", canvas);
    auto* rect = object->AddComponent<IRectTransformComponent>();
    rect->SetAnchoredPosition({100, 60});
    rect->SetSizeDelta({160, 80});
    auto* button = object->AddComponent<IButtonComponent>();
    Check(button && button->GetInteractable() && !button->IsPressed(), "Cannot create Button");
    auto* label = scene->CreateGameObject("Text", object);
    auto* text = label->AddComponent<ITextComponent>();
    Check(text && text->SetText("开始") && !text->SetText("\xED\xA0\x80") && text->GetText()=="开始" &&
        !text->SetFontSize(0) && text->GetFontSize()==DefaultUIFontSize, "Text API did not preserve valid values");

    std::string error;
    Check(runtime.BeginExecution(error), error.c_str());
    runtime.SetUIInputViewport(scene, {400, 300});
    Check(runtime.SubmitInputEvent(Focus(InputEventType::FocusGained)), "UI viewport focus failed");
    const auto pointer = [&](bool down, float x, float y) {
        auto move = Focus(InputEventType::MouseMove);
        move.X=x;
        move.Y=y;
        Check(runtime.SubmitInputEvent(move) &&
            runtime.SubmitInputEvent(Focus(down ? InputEventType::MouseButtonDown : InputEventType::MouseButtonUp)),
            "UI pointer event rejected");
    };
    const auto frame = [&] { runtime.AdvanceFrame(1.0f/60.0f, 0, 0); runtime.CompleteInputFrame(); };
    int clicks = 0;
    std::uint64_t token = 0;
    token = button->RegisterClick([&] { ++clicks; button->UnregisterClick(token); text->SetText("Clicked"); });
    Check(token != 0, "Cannot subscribe to a button click");
    pointer(true, 250, 120);
    Check(!button->IsPressed(), "Button callback escaped host-frame boundary");
    frame();
    Check(button->IsPressed(), "Pointer missed the anchored and scaled Button");
    pointer(false, 200, 150);
    frame();
    Check(!button->IsPressed() && clicks==0, "Release outside incorrectly clicked");
    pointer(true, 250, 120);
    pointer(false, 250, 120);
    frame();
    Check(clicks==1 && text->GetText()=="Clicked", "Two edges within one frame lost a click");
    pointer(true, 250, 120);
    frame();
    runtime.SubmitInputEvent(Focus(InputEventType::FocusLost));
    Check(!button->IsPressed(), "Focus loss retained a UI press");
    runtime.SubmitInputEvent(Focus(InputEventType::FocusGained));

    const auto objectID = object->GetInstanceID();
    Check(button->RegisterClick([&] { ++clicks; scene->DestroyGameObject(object); })!=0,
        "Cannot subscribe destructive click");
    button->SetInteractable(false);
    pointer(true, 250, 120);
    pointer(false, 250, 120);
    frame();
    Check(clicks==1 && runtime.QueryGameObject(objectID), "Disabled Button received a click");
    button->SetInteractable(true);
    pointer(true, 250, 120);
    pointer(false, 250, 120);
    frame();
    Check(clicks==2 && !runtime.QueryGameObject(objectID), "Click did not safely destroy its Button");

    auto* group = scene->CreateGameObject("Rotated group", canvas);
    auto* groupRect = group->AddComponent<IRectTransformComponent>();
    groupRect->SetSizeDelta({200, 100});
    Check(group->GetTransform()->SetLocalRotation({0, 0, 0.70710678f, 0.70710678f}), "Cannot rotate UI group");
    auto* child = scene->CreateGameObject("Nested button", group);
    auto* childRect = child->AddComponent<IRectTransformComponent>();
    childRect->SetAnchoredPosition({80, 0});
    childRect->SetSizeDelta({120, 40});
    auto* childButton = child->AddComponent<IButtonComponent>();
    Check(childButton->RegisterClick([&] { ++clicks; })!=0, "Cannot subscribe to rotated Button");
    runtime.SetUIInputViewport(scene, {400, 300}, {800, 450});
    pointer(true, 235, 150);
    pointer(false, 235, 150);
    frame();
    Check(clicks==2, "Rotated Button still hit its unrotated rectangle");
    pointer(true, 200, 135);
    pointer(false, 200, 135);
    frame();
    Check(clicks==3, "Game input did not map the logical pointer to the rotated layout at the render-target aspect");

    UIScreenPreviewGeometry geometry;
    Check(runtime.GetUIScreenPreviewGeometry(scene, child->GetInstanceID(), {600, 300}, geometry, {800, 450}) &&
        std::fabs(geometry.CanvasBounds.Size.X/geometry.CanvasBounds.Size.Y-800.0f/450.0f)<0.001f,
        "Scene preview did not follow the Game aspect");
    const auto rotatedHit = geometry.Pivot + geometry.Right * -50.0f;
    Check(runtime.PickUIScreenPreview(scene, child->GetInstanceID(), {600, 300}, rotatedHit, {800, 450})==child->GetInstanceID(),
        "Scene preview did not hit the same nested rotated rectangle");
    Check(runtime.EndExecution(error), error.c_str());
}
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
        CheckCanvasInput(*runtime);
        delete runtime;runtime=nullptr;
        std::cout<<"PASS gameplay input: mode/focus gating, key/button edges, held state, mouse/wheel accumulation, pause/resume and stop\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}

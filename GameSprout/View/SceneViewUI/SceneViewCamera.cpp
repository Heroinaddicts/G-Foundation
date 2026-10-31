#include "SceneViewCamera.h"
#include <algorithm>
#include <cmath>

namespace {
bool Finite(GameSproutRuntime::Vector3 value) {return std::isfinite(value.X)&&std::isfinite(value.Y)&&std::isfinite(value.Z);}
}
SceneViewCamera::SceneViewCamera(){Reset();}
void SceneViewCamera::Reset() {
    _State={};
    _State.OrbitDistance=_State.Position.Length();
    _State.YawDegrees=GameSproutRuntime::RadiansToDegrees(std::atan2(-_State.Position.X,_State.Position.Z));
    _State.PitchDegrees=GameSproutRuntime::RadiansToDegrees(std::asin(-_State.Position.Y/_State.OrbitDistance));
}
bool SceneViewCamera::SetState(const SceneViewCameraState& value) {
    if(!Finite(value.Position)||!Finite(value.OrbitPivot)||!std::isfinite(value.YawDegrees)||!std::isfinite(value.PitchDegrees)||
       !std::isfinite(value.OrbitDistance)||value.OrbitDistance<0.1f||value.OrbitDistance>1000000||
       !std::isfinite(value.VerticalFovDegrees)||value.VerticalFovDegrees<1||value.VerticalFovDegrees>170||
       !std::isfinite(value.NearPlane)||value.NearPlane<=0||!std::isfinite(value.FarPlane)||value.FarPlane<=value.NearPlane||
       !std::isfinite(value.MoveSpeed)||value.MoveSpeed<=0||value.MoveSpeed>100000)return false;
    _State=value;_State.PitchDegrees=std::clamp(value.PitchDegrees,-89.0f,89.0f);
    _State.YawDegrees=std::remainder(value.YawDegrees,360.0f);
    return true;
}
GameSproutRuntime::Vector3 SceneViewCamera::Forward() const {
    const float yaw=GameSproutRuntime::DegreesToRadians(_State.YawDegrees),pitch=GameSproutRuntime::DegreesToRadians(_State.PitchDegrees);
    return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
}
void SceneViewCamera::UpdatePivot(){_State.OrbitPivot=_State.Position+Forward()*_State.OrbitDistance;}
void SceneViewCamera::Look(float x,float y,bool orbit) {
    if(!std::isfinite(x)||!std::isfinite(y))return;
    _State.YawDegrees=std::remainder(_State.YawDegrees+std::clamp(x,-500.0f,500.0f)*0.2f,360.0f);
    _State.PitchDegrees=std::clamp(_State.PitchDegrees-std::clamp(y,-500.0f,500.0f)*0.2f,-89.0f,89.0f);
    if(orbit)_State.Position=_State.OrbitPivot-Forward()*_State.OrbitDistance;else UpdatePivot();
}
void SceneViewCamera::Pan(float x,float y,float height) {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(height)||height<=0)return;
    GameSproutRuntime::Vector3 right;Forward().Cross({0,1,0}).TryNormalize(right);
    const auto up=right.Cross(Forward());
    const float factor=2*_State.OrbitDistance*std::tan(GameSproutRuntime::DegreesToRadians(_State.VerticalFovDegrees)*0.5f)/height;
    const auto delta=right*(-x*factor)+up*(y*factor);
    if(Finite(_State.Position+delta)&&Finite(_State.OrbitPivot+delta)){_State.Position=_State.Position+delta;_State.OrbitPivot=_State.OrbitPivot+delta;}
}
void SceneViewCamera::Dolly(float steps) {
    if(!std::isfinite(steps))return;
    _State.OrbitDistance=std::clamp(_State.OrbitDistance*std::exp(-std::clamp(steps,-20.0f,20.0f)*0.15f),0.1f,1000000.0f);
    _State.Position=_State.OrbitPivot-Forward()*_State.OrbitDistance;
}
void SceneViewCamera::Move(float forward,float side,float up,bool accelerated,float dt) {
    if(!std::isfinite(dt)||dt<=0)return;
    GameSproutRuntime::Vector3 right;Forward().Cross({0,1,0}).TryNormalize(right);
    GameSproutRuntime::Vector3 direction;
    if(!(Forward()*forward+right*side+GameSproutRuntime::Vector3{0,up,0}).TryNormalize(direction))return;
    const auto next=_State.Position+direction*(_State.MoveSpeed*(accelerated?5:1)*std::min(dt,0.05f));
    if(Finite(next)){_State.Position=next;UpdatePivot();}
}
bool SceneViewCamera::Focus(GameSproutRuntime::Vector3 center,float radius,float aspect) {
    if(!Finite(center)||!std::isfinite(radius)||radius<0||!std::isfinite(aspect)||aspect<=0)return false;
    const float verticalHalf=GameSproutRuntime::DegreesToRadians(_State.VerticalFovDegrees)*0.5f;
    const float horizontalHalf=std::atan(std::tan(verticalHalf)*aspect);
    // 球形包围体必须同时装入水平和垂直视锥；保留当前 yaw/pitch。
    const float fittingHalf=std::min(verticalHalf,horizontalHalf);
    _State.OrbitPivot=center;
    _State.OrbitDistance=std::clamp(std::max(radius,0.5f)/std::sin(fittingHalf)*1.2f,1.0f,1000000.0f);
    _State.Position=center-Forward()*_State.OrbitDistance;
    return true;
}
GameSproutRuntime::Mat4 SceneViewCamera::ViewMatrix() const {
    GameSproutRuntime::Mat4 matrix;
    GameSproutRuntime::TryMakeLookAt(_State.Position,_State.Position+Forward(),{0,1,0},matrix);
    return matrix;
}

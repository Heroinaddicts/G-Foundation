#include "SceneViewCamera.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool Near(GameSproutRuntime::Vector3 a,GameSproutRuntime::Vector3 b){return (a-b).Length()<0.0001f;}
int main() {
    try {
        SceneViewCamera first,second;
        const auto original=first.GetState();
        Check(Near(original.Position+first.Forward()*original.OrbitDistance,original.OrbitPivot),"Default camera pivot is inconsistent");
        const auto eye=original.Position;
        first.Look(80,-30,false);
        Check(Near(first.GetState().Position,eye)&&!Near(first.Forward(),second.Forward()),"Free look moved the eye or changed another view");
        const auto pivot=first.GetState().OrbitPivot;
        first.Look(-25,50,true);
        Check(Near(first.GetState().OrbitPivot,pivot)&&Near(first.GetState().Position+first.Forward()*first.GetState().OrbitDistance,pivot),"Orbit did not preserve pivot/distance");
        const auto beforePan=first.GetState();first.Pan(30,20,600);
        Check(Near(first.GetState().Position-beforePan.Position,first.GetState().OrbitPivot-beforePan.OrbitPivot),"Pan eye and pivot moved differently");
        const float distance=first.GetState().OrbitDistance;first.Dolly(1);
        Check(first.GetState().OrbitDistance<distance,"Wheel dolly did not approach pivot");
        first.Reset();second.Reset();first.Move(1,0,0,false,0.01f);second.Move(1,0,0,true,0.01f);
        Check(std::fabs((second.GetState().Position-eye).Length()/(first.GetState().Position-eye).Length()-5)<0.001f,"Shift acceleration is inconsistent");
        first.Reset();first.Move(1,0,0,false,1000);
        Check((first.GetState().Position-eye).Length()<=original.MoveSpeed*0.05f+0.0001f,"A resumed frame caused a large camera jump");
        const auto valid=first.GetState();first.Move(1,0,0,false,std::numeric_limits<float>::quiet_NaN());
        Check(Near(valid.Position,first.GetState().Position),"Invalid dt changed camera");
        for(int index=0;index<100;++index)first.Look(0,500,true);
        Check(first.GetState().PitchDegrees>=-89&&first.GetState().PitchDegrees<=89,"Camera pitch became singular");
        Check(first.Focus({3,2,1},2)&&Near(first.GetState().OrbitPivot,{3,2,1}),"F focus did not center selection");
        const auto square=first.GetState();
        Check(first.Focus({3,2,1},2,0.25f)&&first.GetState().OrbitDistance>square.OrbitDistance,"Narrow viewport framing did not move camera farther back");
        Check(first.GetState().YawDegrees==square.YawDegrees&&first.GetState().PitchDegrees==square.PitchDegrees,"Framing changed the Scene camera angle");
        const auto beforeInvalidAspect=first.GetState();
        Check(!first.Focus({1,2,3},2,0)&&Near(beforeInvalidAspect.Position,first.GetState().Position),"Invalid focus aspect changed camera state");
        const auto view=first.ViewMatrix();const auto transformed=view*GameSproutRuntime::Vector4(first.GetState().Position.X,first.GetState().Position.Y,first.GetState().Position.Z,1);
        Check(std::fabs(transformed.X)<0.0001&&std::fabs(transformed.Y)<0.0001&&std::fabs(transformed.Z)<0.0001,"View matrix does not map eye to origin");
        // Unity world orientation is +Z forward, while the published render
        // view maps that direction to -Z without mirroring screen horizontal.
        auto axes=first.GetState();axes.Position={0,0,-3};axes.OrbitPivot={};axes.OrbitDistance=3;
        axes.YawDegrees=0;axes.PitchDegrees=0;
        Check(first.SetState(axes)&&Near(first.Forward(),{0,0,1}),"Zero Scene camera rotation must face Unity +Z");
        auto unityView=first.ViewMatrix();
        auto ahead=unityView*GameSproutRuntime::Vector4{0,0,0,1};
        auto right=unityView*GameSproutRuntime::Vector4{1,0,0,1};
        Check(std::fabs(ahead.Z+3)<.0001f&&std::fabs(right.X-1)<.0001f,"Unity forward did not map to -Z view space with +X screen right");
        first.Move(0,1,0,false,.01f);
        Check(first.GetState().Position.X>0&&std::fabs(first.GetState().Position.Z+3)<.0001f,"D movement did not follow Unity camera right");
        Check(first.SetState(axes),"Reset axis camera");first.Pan(-10,0,600);
        Check(first.GetState().Position.X>0,"Camera pan inverted the Unity +X axis");
        Check(first.SetState(axes),"Reset axis camera");first.Look(50,25,false);
        Check(std::fabs(first.GetState().YawDegrees-10)<.0001f&&std::fabs(first.GetState().PitchDegrees-5)<.0001f&&
            first.Forward().X>0&&first.Forward().Y<0,"Mouse right/down did not match Unity yaw/pitch directions");
        axes.YawDegrees=90;Check(first.SetState(axes)&&Near(first.Forward(),{1,0,0}),"Positive Y 90 degrees must face Unity +X");
        axes.YawDegrees=0;axes.PitchDegrees=30;
        Check(first.SetState(axes)&&Near(first.Forward(),{0,-.5f,std::sqrt(.75f)}),"Positive X pitch must look down in Unity coordinates");
        auto invalid=first.GetState();invalid.NearPlane=0;
        Check(!first.SetState(invalid),"Invalid projection settings accepted");
        invalid=first.GetState();invalid.Position.X=std::numeric_limits<float>::infinity();Check(!first.SetState(invalid),"Nonfinite camera position accepted");
        std::cout<<"PASS independent value cameras, free look/orbit/pan/dolly, movement limits and focus matrices\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

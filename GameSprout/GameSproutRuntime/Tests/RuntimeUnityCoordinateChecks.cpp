#include "GameSproutRuntimeExport.h"
#include "Math/TransformMath.h"
#include "SceneDocument.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
bool Near(float a,float b) {return std::fabs(a-b)<0.0001f;}
void Vector(GameSproutRuntime::Vector4 value,GameSproutRuntime::Vector3 expected) {
    Check(Near(value.X,expected.X)&&Near(value.Y,expected.Y)&&Near(value.Z,expected.Z),"Unity-coordinate vector mismatch");
}
void Matrix(const GameSproutRuntime::Mat4& a,const GameSproutRuntime::Mat4& b) {
    for(unsigned index=0;index<16;++index)Check(Near(a.Data[index],b.Data[index]),"Unity-coordinate matrix mismatch");
}
GameSproutRuntime::Quaternion UnityEuler(float x,float y,float z) {
    // Unity Runtime/Math/Quaternion.cpp: kOrderZXY -> qY * qX * qZ.
    const double rx=x*GameSproutRuntime::Pi/360,ry=y*GameSproutRuntime::Pi/360,rz=z*GameSproutRuntime::Pi/360;
    const double sx=std::sin(rx),cx=std::cos(rx),sy=std::sin(ry),cy=std::cos(ry),sz=std::sin(rz),cz=std::cos(rz);
    return {float(cy*sx*cz+sy*cx*sz),float(sy*cx*cz-cy*sx*sz),
        float(cy*cx*sz-sy*sx*cz),float(cy*cx*cz+sy*sx*sz)};
}
}

int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        using namespace GameSproutRuntime;
        static_assert(RuntimeCoordinates.Hand==Handedness::LeftHanded&&RuntimeCoordinates.Forward==Axis::PositiveZ);
        Check(argc==2,"Pass the Runtime SDK binary directory");
        Quaternion look;
        Check(RuntimeMath::TryMakeLookRotation({0,0,1},{0,1,0},look),"Unity identity LookRotation failed");
        Matrix(RuntimeMath::ComposeTRS({},look,{1,1,1}),{});
        Check(RuntimeMath::TryMakeLookRotation({1,0,0},{0,1,0},look),"Unity +X LookRotation failed");
        Vector(RuntimeMath::ComposeTRS({},look,{1,1,1})*Vector4{0,0,1,0},{1,0,0});
        const auto saved=look;
        Check(!RuntimeMath::TryMakeLookRotation({},{0,1,0},look)&&look.X==saved.X&&look.W==saved.W,"Invalid LookRotation changed output");
        Check(!RuntimeMath::TryMakeLookRotation({0,1,0},{0,2,0},look),"Collinear LookRotation accepted");
        Mat4 view,zReflection;zReflection(2,2)=-1;
        Check(TryMakeLookAt({},{0,0,1},{0,1,0},view),"Unity +Z LookAt failed");Matrix(view,zReflection);
        Vector(view*Vector4{1,0,5,1},{1,0,-5});

        runtime=Export(argv[1]);Check(runtime,"Runtime SDK load failed");
        auto* scene=runtime->CreateScene("Unity coordinate regression");Check(scene,"Scene creation failed");
        auto* object=scene->CreateGameObject("Main Camera");auto* transform=object->GetTransform();
        auto* camera=object->AddComponent<ICameraComponent>();Check(camera,"Camera creation failed");
        transform->SetLocalPosition({-3,6,2});Check(transform->SetLocalRotation(UnityEuler(10.349f,-86.8f,0)),"Screenshot quaternion rejected");
        Check(camera->GetViewMatrix(view),"Screenshot camera view failed");
        const auto rotation=RuntimeMath::ComposeTRS({},transform->GetLocalRotation(),{1,1,1});
        const auto forward=rotation*Vector4{0,0,1,0};
        Vector(forward,{-0.98219788f,-0.17964359f,0.05491324f});
        const auto eye=transform->GetLocalPosition();
        Vector(view*Vector4{eye.X,eye.Y,eye.Z,1},{});
        Vector(view*Vector4{eye.X+forward.X*10,eye.Y+forward.Y*10,eye.Z+forward.Z*10,1},{0,0,-10});

        auto* parent=scene->CreateGameObject("Scaled camera parent");
        parent->GetTransform()->SetLocalPosition({3,2,1});parent->GetTransform()->SetLocalRotation(UnityEuler(0,45,0));
        parent->GetTransform()->SetLocalScale({-2,3,0.5f});Check(object->SetParent(parent,false),"Camera reparent failed");
        Check(camera->GetViewMatrix(view),"Scaled-parent camera view failed");
        const auto world=transform->GetWorldMatrix();
        auto pose=RuntimeMath::ComposeTRS({},parent->GetTransform()->GetLocalRotation(),{1,1,1})*rotation;
        for(unsigned row=0;row<3;++row)pose(row,3)=world(row,3);
        Mat4 inverse;Check(RuntimeMath::TryInverseAffine(pose,inverse),"Expected no-scale pose failed");Matrix(view,zReflection*inverse);
        const auto before=view;
        transform->SetLocalScale({0,-4,7});
        Check(camera->GetViewMatrix(view),"Zero/negative camera scale incorrectly invalidated view");Matrix(view,before);
        const auto parentForward=pose*Vector4{0,0,1,0};
        Mat4 explicitView;
        Check(TryMakeLookAt({world(0,3),world(1,3),world(2,3)},
            {world(0,3)+parentForward.X,world(1,3)+parentForward.Y,world(2,3)+parentForward.Z},
            {pose(0,1),pose(1,1),pose(2,1)},explicitView),"Explicit camera view failed");Matrix(view,explicitView);

        const Vector3 cameraEye{world(0,3),world(1,3),world(2,3)};
        const Vector3 target=cameraEye+Vector3{3,1,-4};
        const Vector3 targetUp{0.25f,1,0};
        Check(camera->LookAt(target,targetUp)&&camera->GetViewMatrix(view),"Camera LookAt under scaled parent failed");
        Check(TryMakeLookAt(cameraEye,target,targetUp,explicitView),"Expected LookAt matrix failed");
        Matrix(view,explicitView);
        const auto aimedWorld=transform->GetWorldMatrix();
        Vector({aimedWorld(0,3),aimedWorld(1,3),aimedWorld(2,3),1},cameraEye);
        Check(!camera->LookAt(cameraEye)&&!camera->LookAt(cameraEye+Vector3{0,1,0})&&
            !camera->LookAt(target,{}),"Camera LookAt accepted degenerate inputs");
        Check(camera->GetViewMatrix(view),"Invalid Camera LookAt damaged view");
        Matrix(view,explicitView);

        std::string error;auto document=SceneDocument::CreateDefault("Current format");
        Check(document["Version"].asUInt()==SceneDocumentVersion&&SceneDocument::Validate(document,error),"Current Unity scene invalid: "+error);
        document["Version"]=1;
        Check(!SceneDocument::Validate(document,error),"Old right-handed scene was silently reinterpreted");
        delete runtime;runtime=nullptr;
        std::cout<<"PASS Unity +Z/positive yaw, screenshot camera, no-scale parent hierarchy, LookAt/LookRotation and current-format rejection\n";
        return 0;
    } catch(const std::exception& exception) {delete runtime;std::cerr<<exception.what()<<'\n';return 1;}
}

#include "../Renderer/RuntimeShadowsMath.h"
#include "../Renderer/RuntimeFrustum.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;

namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool Close(float a,float b,float epsilon=1e-4f){return std::fabs(a-b)<=epsilon;}
void Contained(const RuntimeShadowMath::Cascade& cascade,Vector3 world){
    const auto clip=cascade.ViewProjection*Vector4{world.X,world.Y,world.Z,1};
    Check(std::fabs(clip.X)<=1.0001f&&std::fabs(clip.Y)<=1.0001f&&clip.Z>=-.0001f&&clip.Z<=1.0001f,
        "Receiver/caster point escaped the fitted canonical light frustum");
}
void CheckReceiverCorners(const RenderCamera& camera,float aspect,const RuntimeShadowMath::Cascade& cascade){
    Mat4 inverse;
    Check(RuntimeMath::TryInverseAffine(camera.View,inverse),"Invalid test view");
    for(unsigned index=0;index<8;++index){
        const float depth=index<4?cascade.NearDepth:cascade.FarDepth;
        const float height=camera.Projection.Projection==CameraProjection::Perspective?
            std::tan(camera.Projection.VerticalFovRadians*.5f)*depth:camera.Projection.OrthographicHeight*.5f;
        const auto world=inverse*Vector4{(index&1?height:-height)*aspect,index&2?height:-height,-depth,1};
        Contained(cascade,{world.X,world.Y,world.Z});
    }
}
}

int main(){
    try{
        RenderCamera camera;camera.Projection.NearPlane=.1f;camera.Projection.FarPlane=1000;
        RuntimeShadowMath::CascadeSet cascades;
        for(auto count:{1u,2u,4u}){
            Check(RuntimeShadowMath::Build(camera,16.0f/9,{.3f,.8f,.6f},50,count,1024,nullptr,0,cascades),
                "Perspective cascade generation failed");
            Check(cascades.Count==count&&Close(cascades.Entries.front().NearDepth,.1f)&&
                Close(cascades.Entries[count-1].FarDepth,50),"Cascade camera-depth endpoints are incorrect");
            for(unsigned index=0;index<count;++index){
                const auto& entry=cascades.Entries[index];
                Check(entry.FarDepth>entry.NearDepth&&entry.WorldUnitsPerTexel>0&&entry.DepthRange>0&&
                    Close(entry.WorldUnitsPerTexel,entry.Radius*2/1024),"Invalid cascade scale/bias metadata");
                if(index)Check(entry.NearDepth==cascades.Entries[index-1].FarDepth,"Gap between cascade intervals");
                CheckReceiverCorners(camera,16.0f/9,entry);
            }
        }
        Check(TryMakeLookAt({9,7,11},{1,0,-3},{0,1,0},camera.View),"Rotated camera setup failed");
        Check(RuntimeShadowMath::Build(camera,1.3f,{0,-1,0},20,4,512,nullptr,0,cascades),
            "Vertical light or rotated view produced a singular light basis");
        for(unsigned index=0;index<cascades.Count;++index)CheckReceiverCorners(camera,1.3f,cascades.Entries[index]);

        camera.View={};camera.Projection.Projection=CameraProjection::Orthographic;
        camera.Projection.NearPlane=0;camera.Projection.FarPlane=40;camera.Projection.OrthographicHeight=12;
        Check(RuntimeShadowMath::Build(camera,2,{0,1,0},80,4,512,nullptr,0,cascades),"Orthographic near-zero view failed");
        for(unsigned index=0;index<4;++index){
            Check(Close(cascades.Entries[index].FarDepth,float(index+1)*10),"Orthographic cascade splits were not uniform/camera-far limited");
            CheckReceiverCorners(camera,2,cascades.Entries[index]);
        }

        camera={};camera.Projection.FarPlane=50;
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,1,1024,nullptr,0,cascades),"Stable fit setup failed");
        const auto original=cascades.Entries[0];
        camera.View(0,3)=-original.WorldUnitsPerTexel*.1f;
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,1,1024,nullptr,0,cascades),"Small translation failed");
        for(unsigned row=0;row<2;++row)for(unsigned column=0;column<4;++column)
            Check(original.ViewProjection(row,column)==cascades.Entries[0].ViewProjection(row,column),
                "Sub-texel camera motion moved the snapped light XY transform");

        camera.View={};
        const Bounds aboveView{{0,100,-10},{1,1,1}};
        Mat4 cameraProjection;Check(TryMakePerspective(camera.Projection.VerticalFovRadians,1,.1f,50,cameraProjection),"Camera setup failed");
        const RenderFrustum::Frustum cameraFrustum(cameraProjection);
        Check(!cameraFrustum.Intersects(aboveView.Min(),aboveView.Max(),{}),"Caster fixture is not outside the camera view");
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,1,1024,&aboveView,1,cascades),
            "Off-camera casting bounds failed");
        Contained(cascades.Entries[0],{0,101,-10});
        Check(cascades.Entries[0].DepthRange>original.DepthRange&&Close(cascades.Entries[0].Radius,original.Radius),
            "Off-camera caster failed to extend depth, or expanded the stable XY receiver fit");
        const Bounds unrelated{{10000,10000,-10},{1,1,1}};
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,1,1024,&unrelated,1,cascades)&&
            Close(cascades.Entries[0].DepthRange,original.DepthRange),"Unrelated caster expanded shadow depth precision unnecessarily");

        const auto preserved=cascades;
        const auto rejected=[&](const RenderCamera& input,float aspect,Vector3 light,float distance,unsigned count,unsigned resolution,
            const Bounds* bounds=nullptr,std::size_t size=0){
            Check(!RuntimeShadowMath::Build(input,aspect,light,distance,count,resolution,bounds,size,cascades),"Invalid input was accepted");
            Check(cascades.Count==preserved.Count&&cascades.Entries[0].DepthRange==preserved.Entries[0].DepthRange&&
                cascades.Entries[0].ViewProjection(0,0)==preserved.Entries[0].ViewProjection(0,0),"Rejected input partially changed output");
        };
        rejected(camera,0,{0,1,0},50,4,1024);rejected(camera,1,{},50,4,1024);
        rejected(camera,1,{0,1,0},.05f,4,1024);rejected(camera,1,{0,1,0},50,3,1024);
        rejected(camera,1,{0,1,0},50,4,4);rejected(camera,1,{0,1,0},50,4,1024,nullptr,1);
        auto invalid=camera;invalid.View(0,0)=0;rejected(invalid,1,{0,1,0},50,4,1024);
        invalid=camera;invalid.Projection.VerticalFovRadians=Pi;rejected(invalid,1,{0,1,0},50,4,1024);
        invalid=camera;invalid.Projection.FarPlane=std::numeric_limits<float>::infinity();rejected(invalid,1,{0,1,0},50,4,1024);
        const Bounds negative{{},{-1,1,1}};rejected(camera,1,{0,1,0},50,4,1024,&negative,1);
        std::cout<<"PASS directional shadow math: 1/2/4 cascades, perspective/orthographic, canonical containment, texel stability, off-camera casters and transactional rejection\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}

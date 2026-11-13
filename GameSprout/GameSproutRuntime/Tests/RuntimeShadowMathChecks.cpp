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

        camera.Projection.NearPlane=2;
        constexpr std::array<float,3> authoredSplits{.125f,.375f,.75f};
        for(auto projection:{CameraProjection::Perspective,CameraProjection::Orthographic}) {
            camera.Projection.Projection=projection;
            Check(RuntimeShadowMath::Build(camera,2,{0,1,0},80,4,512,nullptr,0,cascades,1,nullptr,0xF,0,authoredSplits),
                "Manual cascade splits failed");
            constexpr std::array<float,4> depths{6.75f,16.25f,30.5f,40};
            for(unsigned index=0;index<4;++index) {
                const auto& cascade=cascades.Entries[index];
                Check(Close(cascade.NearDepth,index?depths[index-1]:2)&&Close(cascade.FarDepth,depths[index]),
                    "Manual splits did not use the effective near-to-far interval");
                CheckReceiverCorners(camera,2,cascade);
            }
        }
        const auto manual=cascades;
        Check(RuntimeShadowMath::Build(camera,2,{0,1,0},80,4,512,nullptr,0,cascades,1,nullptr,0x6,0,authoredSplits),
            "Selective fitting with manual splits failed");
        for(unsigned index=0;index<4;++index)
            Check(cascades.Entries[index].NearDepth==manual.Entries[index].NearDepth&&
                cascades.Entries[index].FarDepth==manual.Entries[index].FarDepth,"Selective fitting changed the manual depth partition");
        Check(RuntimeShadowMath::Build(camera,2,{0,1,0},30,2,512,nullptr,0,cascades,1,nullptr,0xF,0,{.25f,0,0})&&
            Close(cascades.Entries[0].FarDepth,9)&&Close(cascades.Entries[1].FarDepth,30),
            "Manual split did not respect a shorter shadow distance");
        const float mediumRadius=cascades.Entries[0].Radius;
        Check(RuntimeShadowMath::Build(camera,2,{0,1,0},30,2,512,nullptr,0,cascades,1,nullptr,0xF,0,{.25f,0,0},2.0f)&&
            cascades.Entries[0].Radius>mediumRadius,"High-quality filter footprint did not increase receiver padding");

        const LightShadowSettings defaults;
        Check(defaults.StaticUpdateMode==StaticShadowUpdateMode::AutomaticIncremental&&
            defaults.FilterQuality==ShadowFilterQuality::Medium&&defaults.CascadeSplits==std::array<float,3>{}&&
            Close(defaults.FadeStart,.9f)&&IsValidLightShadows(defaults),"Incorrect public shadow defaults");
        auto custom=defaults;
        custom.CascadeSplits=authoredSplits;
        custom.StaticUpdateMode=StaticShadowUpdateMode::FullCascade;
        custom.FilterQuality=ShadowFilterQuality::High;
        for(float fade:{0.f,1.f}) {
            custom.FadeStart=fade;
            Check(IsValidLightShadows(custom),"Manual cascade splits or fade endpoint were rejected");
        }
        constexpr std::array<std::array<float,3>,5> invalidSplits{{{.25f,0,.75f},
            {.25f,.25f,.75f},{.5f,.25f,.75f},{.25f,.5f,1},{-.25f,.5f,.75f}}};
        for(const auto& invalid:invalidSplits) {
            custom.CascadeSplits=invalid;
            Check(!IsValidLightShadows(custom),"Partial, unsorted or out-of-range cascade splits were accepted");
        }
        custom.CascadeCount=2;
        custom.CascadeSplits={.25f,0,0};
        Check(IsValidLightShadows(custom),"Two cascades must accept a single manual split");
        custom.CascadeSplits[1]=.75f;
        Check(!IsValidLightShadows(custom),"Unused split entry was accepted");
        custom.CascadeSplits={};
        custom.FadeStart=std::numeric_limits<float>::quiet_NaN();
        Check(!IsValidLightShadows(custom),"Nonfinite fade start was accepted");
        custom.FadeStart=1.01f;
        Check(!IsValidLightShadows(custom),"Out-of-range fade start was accepted");
        custom.FadeStart=.9f;
        custom.FilterQuality=static_cast<ShadowFilterQuality>(255);
        Check(!IsValidLightShadows(custom),"Unknown shadow filter quality was accepted");
        custom.FilterQuality=ShadowFilterQuality::Low;
        custom.StaticUpdateMode=static_cast<StaticShadowUpdateMode>(255);
        Check(!IsValidLightShadows(custom),"Unknown static shadow update mode was accepted");

        camera={};camera.Projection.FarPlane=50;
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,1,1024,nullptr,0,cascades),"Stable fit setup failed");
        const auto original=cascades.Entries[0];
        camera.View(0,3)=-original.WorldUnitsPerTexel*.1f;
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,1,1024,nullptr,0,cascades),"Small translation failed");
        for(unsigned row=0;row<2;++row)for(unsigned column=0;column<4;++column)
            Check(original.ViewProjection(row,column)==cascades.Entries[0].ViewProjection(row,column),
                "Sub-texel camera motion moved the snapped light XY transform");

        camera.View={};
        RuntimeShadowMath::CascadeSet guarded;
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,4,512,nullptr,0,guarded,1.125),"Guarded cascade generation failed");
        Mat4 staticProjection[4];float splits[4];
        for(unsigned index=0;index<4;++index){
            staticProjection[index]=guarded.Entries[index].ViewProjection;
            splits[index]=guarded.Entries[index].FarDepth;
        }
        camera.View(0,3)=-.25f;
        Check(RuntimeShadowMath::CoveredCascades(camera,1,staticProjection,splits,4,512,1,1.0f)==0xF,
            "Small camera motion escaped guarded coverage");
        const Bounds movingCaster{{.25f,20,-6},{.5f,.8f,.5f}};
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,4,512,&movingCaster,1,cascades,1,staticProjection),
            "Moving caster depth fit failed in the retained tile");
        for(unsigned index=0;index<4;++index){
            for(unsigned row=0;row<2;++row)for(unsigned column=0;column<4;++column)
                Check(cascades.Entries[index].ViewProjection(row,column)==staticProjection[index](row,column),
                    "Dynamic depth fit moved the cached static UV coordinates");
            CheckReceiverCorners(camera,1,cascades.Entries[index]);
        }
        camera.View(0,3)=-4;
        const auto covered=RuntimeShadowMath::CoveredCascades(camera,1,staticProjection,splits,4,512,1,1.0f);
        Check(covered==0xE,"Moderate movement should invalidate only the nearest cascade");
        Check(RuntimeShadowMath::Build(camera,1,{0,1,0},50,4,512,nullptr,0,cascades,1.125,nullptr,0xF&~covered),
            "Refitting only the uncovered cascade failed");
        for(unsigned index=0;index<4;++index) {
            Check(cascades.Entries[index].FarDepth==splits[index],"Partial fitting changed the depth partition");
            if(!(covered&(1u<<index)))staticProjection[index]=cascades.Entries[index].ViewProjection;
        }
        Check(RuntimeShadowMath::CoveredCascades(camera,1,staticProjection,splits,4,512,1,1.0f)==0xF,
            "Retaining three projections after partial fitting left missing coverage");
        camera.View(0,3)=-100;
        Check(RuntimeShadowMath::CoveredCascades(camera,1,staticProjection,splits,4,512,1,1.0f)==0,
            "Camera teleport reused an uncovered light tile");
        camera.View(0,3)=0;camera.View(1,3)=-100;
        Check(RuntimeShadowMath::CoveredCascades(camera,1,staticProjection,splits,4,512,1,1.0f)==0,
            "Camera motion escaped only light depth without refitting");

        // A wide Game view should keep the far shadow footprint when yawing
        // and pitching, including a small third-person orbit around the eye.
        camera={};camera.Projection.FarPlane=120;
        constexpr float wideAspect=2.2f;
        constexpr std::uint32_t farCascades=0xE;
        const Vector3 eye{23,7,-19};
        Check(TryMakeLookAt(eye,eye+Vector3{0,-.25f,1},{0,1,0},camera.View),"Initial orbit view failed");
        Check(RuntimeShadowMath::Build(camera,wideAspect,{.3f,.8f,.6f},120,4,1024,nullptr,0,
            guarded,1.125,nullptr,0xF,farCascades),"Rotation-stable far cascade generation failed");
        for(unsigned index=0;index<4;++index){
            staticProjection[index]=guarded.Entries[index].ViewProjection;
            splits[index]=guarded.Entries[index].FarDepth;
        }
        for(unsigned step=0;step<24;++step) {
            const float yaw=2*Pi*step/24;
            const Vector3 direction{std::sin(yaw),.55f*std::sin(yaw*2),std::cos(yaw)};
            const auto offset=Vector3{std::cos(yaw)*4,0,std::sin(yaw)*4};
            Check(TryMakeLookAt(eye+offset,eye+offset+direction,{0,1,0},camera.View),"Rotated orbit view failed");
            const auto mask=RuntimeShadowMath::CoveredCascades(camera,wideAspect,staticProjection,splits,4,1024,2,1.0f);
            Check((mask&farCascades)==farCascades,"Turn or small orbit escaped a rotation-stable far cascade");
        }
        const Vector3 distantEye{500,7,-19};
        Check(TryMakeLookAt(distantEye,distantEye+Vector3{0,0,1},{0,1,0},camera.View),"Teleported orbit view failed");
        Check((RuntimeShadowMath::CoveredCascades(camera,wideAspect,staticProjection,splits,4,1024,2,1.0f)&farCascades)==0,
            "A large translation incorrectly retained rotation-stable far cascades");

        constexpr unsigned scrollTile=1024;
        Mat4 oldScroll,newScroll;
        oldScroll(0,3)=.125f;oldScroll(1,3)=-.25f;
        newScroll=oldScroll;
        newScroll(0,3)-=76.f*2/scrollTile;
        newScroll(1,3)+=19.f*2/scrollTile;
        int scrollX=0,scrollY=0;
        Check(RuntimeShadowMath::TryShadowScroll(oldScroll,newScroll,scrollTile,scrollX,scrollY)&&scrollX==-76&&scrollY==-19,
            "Integer light-space translation returned wrong top-first texel offset");
        Check(RuntimeShadowMath::TryShadowScroll(newScroll,oldScroll,scrollTile,scrollX,scrollY)&&scrollX==76&&scrollY==19,
            "Reversing a scroll did not reverse its texel offset");
        Check(RuntimeShadowMath::TryShadowScroll(oldScroll,oldScroll,scrollTile,scrollX,scrollY)&&!scrollX&&!scrollY,
            "Unchanged projection returned a scroll");
        const auto rejectScroll=[&](Mat4 candidate,std::size_t row,std::size_t column,float change) {
            candidate(row,column)+=change;
            Check(!RuntimeShadowMath::TryShadowScroll(oldScroll,candidate,scrollTile,scrollX,scrollY),
                "Projection requiring a full redraw was accepted for scrolling");
        };
        rejectScroll(newScroll,0,0,.25f);
        rejectScroll(newScroll,1,2,.25f);
        rejectScroll(newScroll,2,3,.25f);
        rejectScroll(newScroll,3,2,.25f);
        rejectScroll(oldScroll,1,3,1.f/scrollTile);
        rejectScroll(oldScroll,0,3,257.f*2/scrollTile);
        Check(RuntimeShadowMath::WrapShadowTexel(-1,scrollTile)==1023&&
            RuntimeShadowMath::WrapShadowTexel(1024,scrollTile)==0&&
            RuntimeShadowMath::WrapShadowTexel(2049,scrollTile)==1&&
            RuntimeShadowMath::WrapShadowTexel(-2049,scrollTile)==1023,
            "Physical shadow texel did not wrap at either edge");

        camera.View={};
        camera.Projection.FarPlane=50;
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

        // A long rotated mesh lies outside the cascade on its short axis.
        // Expanding to world axes first makes its bound overlap the tile.
        const float diagonal=std::sqrt(.5f);
        Mat4 lightProjection,model;
        lightProjection(0,0)=lightProjection(0,1)=lightProjection(1,1)=diagonal;
        lightProjection(1,0)=-diagonal;
        model(0,0)=-diagonal;model(1,0)=model(0,1)=model(1,1)=diagonal;
        model(0,3)=model(1,3)=1.35f*diagonal;
        model(2,3)=.5f;
        const Vector3 localExtent{2,.25f,.2f};
        const Bounds loose{{model(0,3),model(1,3),model(2,3)},
            {2.25f*diagonal,2.25f*diagonal,.2f}};
        const RenderFrustum::Frustum lightFrustum(lightProjection);
        Check(lightFrustum.Intersects(loose.Min(),loose.Max(),{})&&
            !lightFrustum.Intersects(-localExtent,localExtent,model),
            "Rotated shadow caster retained the world-AABB false positive");
        model(0,3)=model(1,3)=1.24999f*diagonal;
        Check(lightFrustum.Intersects(-localExtent,localExtent,model),
            "Mirrored caster crossing the cascade edge was dropped");

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
        std::cout<<"PASS directional shadow math: automatic/manual cascades, shadow controls validation, canonical containment, texel stability, off-camera casters and transactional rejection\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}

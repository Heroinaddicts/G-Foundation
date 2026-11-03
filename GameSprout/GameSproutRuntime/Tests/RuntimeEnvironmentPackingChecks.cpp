#include "../Renderer/RuntimeEnvironment.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace GameSproutRuntime;
namespace {void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}}
int main(){
    try {
        EnvironmentLightingSettings settings;
        RenderEnvironment::Uniform packed;
        Check(RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==0&&packed.AmbientColor.X==0,"None uploaded default ambient color");
        settings.Mode=AmbientLightMode::Color;settings.AmbientColor={.5f,.25f,2,1};settings.Intensity=2;
        Check(RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==1&&packed.AmbientColor.X==1&&packed.AmbientColor.Y==.5f&&packed.AmbientColor.Z==4,
            "Linear/HDR ambient color was decoded or intensity was not applied exactly once");
        Check(packed.AmbientColor.W==0&&packed.Info.Y==0&&packed.Info.Z==0&&packed.Info.W==0,"GPU block padding retained alpha or undefined values");
        settings.Mode=AmbientLightMode::Gradient;settings.SkyColor={1,0,0};settings.EquatorColor={0,2,0};settings.GroundColor={0,0,3};
        Check(RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==2&&packed.SkyColor.X==2&&packed.EquatorColor.Y==4&&packed.GroundColor.Z==6,
            "Gradient colors were conflated or not scaled once");
        settings.Mode=AmbientLightMode::SphericalHarmonics;settings.SHCoefficients[0]={.5f,-.25f,2};settings.SHCoefficients[8]={-1,.75f,-.5f};
        Check(RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==3&&packed.SH[0].X==1&&packed.SH[0].Y==-.5f&&packed.SH[0].Z==4&&
            packed.SH[8].X==-2&&packed.SH[8].Y==1.5f&&packed.SH[8].Z==-1,"Signed SH coefficients were normalized, clamped or scaled twice");
        settings.Intensity=0;Check(RenderEnvironment::Prepare(settings,packed)&&packed.SH[0].Z==0&&packed.SH[8].X==0,"Zero intensity retained stale SH lighting");
        settings.Mode=AmbientLightMode::Color;settings.Intensity=std::numeric_limits<float>::max();settings.AmbientColor={2,2,2};
        packed.Info.X=7;Check(!RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==7,"Derived radiance overflow uploaded Infinity or partially committed a block");
        settings.Mode=AmbientLightMode::SphericalHarmonics;settings.Intensity=1;settings.SHCoefficients={};settings.SHCoefficients[6]={std::numeric_limits<float>::max(),0,0};
        Check(!RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==7,"Finite SH terms were allowed to overflow during GPU polynomial evaluation");
        settings.Mode=AmbientLightMode::None;Check(RenderEnvironment::Prepare(settings,packed)&&packed.Info.X==0,"An inactive mode multiplied unused HDR values");
        settings.Atmosphere.Enabled=true;settings.Atmosphere.FogStart=12;settings.Atmosphere.FogEnd=120;
        settings.Atmosphere.FogMaxOpacity=.5f;settings.Atmosphere.FogColor={.1f,.2f,.3f,1};settings.Intensity=0;
        Check(RenderEnvironment::Prepare(settings,packed)&&packed.AtmosphereParameters.X==1&&packed.AtmosphereParameters.Y==12&&
            packed.AtmosphereParameters.Z==120&&packed.AtmosphereParameters.W==.5f&&packed.AtmosphereFogColor.Y==.2f,
            "Atmosphere was disabled or multiplied by diffuse ambient intensity");
        settings.Atmosphere.Enabled=false;Check(RenderEnvironment::Prepare(settings,packed)&&packed.AtmosphereParameters.X==0,
            "Disabled atmosphere retained an enabled GPU state");
        std::cout<<"PASS environment GPU packing: modes, linear HDR, signed SH, intensity/padding and derived overflow\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';return 1;}
}

#pragma once
#include "Api/RuntimeLightingApi.h"
#include "Api/Math/Vector4.h"
#include <array>
#include <limits>

namespace GameSproutRuntime::RenderEnvironment {
// Scene-owned diffuse environment, separate from per-object material/light data.
// No reflection texture or invented specular environment is bound by this block.
struct Uniform {
    Vector4 Info; // X: AmbientLightMode. Remaining components are zero padding.
    Vector4 AmbientColor,SkyColor,EquatorColor,GroundColor;
    std::array<Vector4,9> SH;
};
static_assert(sizeof(Uniform)==224,"Environment fragment block must match HLSL packing");

// Intensity is applied once while preparing this frame's scene state. Overflow
// in derived HDR radiance is diagnosed by the Renderer; no Infinity is uploaded.
// Inactive modes' stored colors/coefficients are deliberately not multiplied.
inline bool Prepare(const EnvironmentLightingSettings& settings,Uniform& output) {
    if(!IsValidEnvironmentLighting(settings))return false;
    Uniform candidate{};
    candidate.Info.X=static_cast<float>(settings.Mode);
    const auto scaled=[&](double value,float& destination) {
        const double result=value*settings.Intensity;
        if(!std::isfinite(result)||std::fabs(result)>std::numeric_limits<float>::max())return false;
        destination=static_cast<float>(result);return true;
    };
    const auto color=[&](Color source,Vector4& destination) {
        return scaled(source.R,destination.X)&&scaled(source.G,destination.Y)&&scaled(source.B,destination.Z);
    };
    switch(settings.Mode) {
    case AmbientLightMode::None:break;
    case AmbientLightMode::Color:
        if(!color(settings.AmbientColor,candidate.AmbientColor))return false;
        break;
    case AmbientLightMode::Gradient:
        if(!color(settings.SkyColor,candidate.SkyColor)||!color(settings.EquatorColor,candidate.EquatorColor)||
           !color(settings.GroundColor,candidate.GroundColor))return false;
        break;
    case AmbientLightMode::SphericalHarmonics:
        for(std::size_t index=0;index<candidate.SH.size();++index) {
            const auto source=settings.SHCoefficients[index];auto& destination=candidate.SH[index];
            if(!scaled(source.X,destination.X)||!scaled(source.Y,destination.Y)||!scaled(source.Z,destination.Z))return false;
        }
        // Signed finite coefficients may still overflow a float shader sum.
        // Bound every term on the unit sphere, including (3*z*z-1) in [-1,2].
        // Keep uncertain extreme HDR configurations out of the GPU evaluator.
        {
            constexpr double basisMaximum[]={1,1,1,1,.5,.5,2,.5,1};
            double maximum[3]{};
            for(std::size_t index=0;index<candidate.SH.size();++index) {
                const auto coefficient=candidate.SH[index];
                maximum[0]+=std::fabs(double(coefficient.X))*basisMaximum[index];
                maximum[1]+=std::fabs(double(coefficient.Y))*basisMaximum[index];
                maximum[2]+=std::fabs(double(coefficient.Z))*basisMaximum[index];
            }
            for(double channel:maximum)if(channel>std::numeric_limits<float>::max())return false;
        }
        break;
    }
    output=candidate;return true;
}
}

#pragma once
#include "GameSproutRuntimeApi.h"
#include <array>
#include <cmath>

namespace GameSproutRuntime {
enum class AmbientLightMode : std::uint8_t { None, Color, Gradient, SphericalHarmonics };

struct EnvironmentLightingSettings {
    AmbientLightMode Mode = AmbientLightMode::None;
    // Linear RGB/HDR, nonnegative and finite; alpha is unused by lighting.
    Color AmbientColor{.2f,.2f,.2f,1};
    Color SkyColor{.2f,.2f,.2f,1};
    Color EquatorColor{.1f,.1f,.1f,1};
    Color GroundColor{.05f,.05f,.05f,1};
    float Intensity = 1;
    // World-space, already diffuse-convolved E/pi polynomial coefficients.
    // Basis: [1,y,z,x,xy,yz,3*z*z-1,xz,x*x-y*y]. Signed RGB is valid.
    // Evaluate directly: do not apply normalization constants or pi again.
    std::array<Vector3,9> SHCoefficients{};
};

inline bool IsValidEnvironmentLighting(const EnvironmentLightingSettings& settings) {
    if(settings.Mode!=AmbientLightMode::None && settings.Mode!=AmbientLightMode::Color &&
       settings.Mode!=AmbientLightMode::Gradient && settings.Mode!=AmbientLightMode::SphericalHarmonics)return false;
    if(!std::isfinite(settings.Intensity)||settings.Intensity<0)return false;
    for(const auto& color:{settings.AmbientColor,settings.SkyColor,settings.EquatorColor,settings.GroundColor})
        if(!std::isfinite(color.R)||!std::isfinite(color.G)||!std::isfinite(color.B)||!std::isfinite(color.A)||
           color.R<0||color.G<0||color.B<0||color.A<0||color.A>1)return false;
    for(const auto& coefficient:settings.SHCoefficients)
        if(!std::isfinite(coefficient.X)||!std::isfinite(coefficient.Y)||!std::isfinite(coefficient.Z))return false;
    return true;
}

enum class LightType : std::uint8_t {
    Directional = 0
};

struct LightShadowSettings {
    bool Enabled = false;
    float Strength = 1;
    // Maximum camera-space shadow distance in metres. Resolution is the
    // complete atlas edge; one, two or four cascades share this atlas.
    float Distance = 50;
    std::uint32_t Resolution = 2048;
    std::uint32_t CascadeCount = 4;
    // Biases are measured in shadow texels, scaled separately for each cascade.
    // Runtime converts them to world/depth offsets; neither value is in metres.
    float DepthBias = .5f;
    float NormalBias = 1;
    bool Soft = true;
    bool operator==(const LightShadowSettings&) const = default;
};

inline bool IsValidLightShadows(const LightShadowSettings& settings) {
    return std::isfinite(settings.Strength)&&settings.Strength>=0&&settings.Strength<=1&&
        std::isfinite(settings.Distance)&&settings.Distance>0&&
        (settings.Resolution==256||settings.Resolution==512||settings.Resolution==1024||
         settings.Resolution==2048||settings.Resolution==4096)&&
        (settings.CascadeCount==1||settings.CascadeCount==2||settings.CascadeCount==4)&&
        std::isfinite(settings.DepthBias)&&settings.DepthBias>=0&&settings.DepthBias<=10&&
        std::isfinite(settings.NormalBias)&&settings.NormalBias>=0&&settings.NormalBias<=10;
}

struct LightSettings {
    LightType Type = LightType::Directional;
    bool Enabled = true;
    // Linear RGB, as defined by Color. HDR channels are allowed; alpha is not
    // used by lighting. The Editor converts its sRGB color picker at the boundary.
    GameSproutRuntime::Color Color{1,1,1,1};
    // URP-compatible relative intensity: the Lambert pi factor is absorbed in
    // the lighting convention. This is not an SI radiometric intensity unit.
    float Intensity = 1.0f;
    // Object Layer membership, independent of the camera's visibility mask.
    std::uint32_t CullingMask = 0xffffffffu;
    LightShadowSettings Shadows;
};

class GAMESPROUT_RUNTIME_PUBLIC_TYPE ILightComponent : public IComponent {
public:
    virtual const LightSettings& GetSettings() const = 0;
    // Invalid/nonfinite/negative values leave the complete state unchanged.
    // A directional light emits along its Transform's local +Z axis. Position
    // and scale do not affect its direction or intensity.
    virtual bool SetSettings(const LightSettings& settings) = 0;

    bool IsEnabled() const { return GetSettings().Enabled; }
    bool SetEnabled(bool enabled) { auto settings=GetSettings();settings.Enabled=enabled;return SetSettings(settings); }
    LightType GetLightType() const { return GetSettings().Type; }
    bool SetLightType(LightType type) { auto settings=GetSettings();settings.Type=type;return SetSettings(settings); }
    GameSproutRuntime::Color GetColor() const { return GetSettings().Color; }
    bool SetColor(GameSproutRuntime::Color color) { auto settings=GetSettings();settings.Color=color;return SetSettings(settings); }
    float GetIntensity() const { return GetSettings().Intensity; }
    bool SetIntensity(float intensity) { auto settings=GetSettings();settings.Intensity=intensity;return SetSettings(settings); }
    std::uint32_t GetCullingMask() const { return GetSettings().CullingMask; }
    bool SetCullingMask(std::uint32_t mask) { auto settings=GetSettings();settings.CullingMask=mask;return SetSettings(settings); }
    const LightShadowSettings& GetShadows() const { return GetSettings().Shadows; }
    bool SetShadows(const LightShadowSettings& shadows) { auto settings=GetSettings();settings.Shadows=shadows;return SetSettings(settings); }

protected:
    using IComponent::IComponent;
};
}

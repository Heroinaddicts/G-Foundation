#include "LightComponent.h"
#include "ComponentFactory.h"
#include "Scene.h"
#include <cmath>

namespace GameSproutRuntime {
LightComponent::LightComponent(IGameObject* owner)
    : BuiltinComponent<ILightComponent>(owner,"GameSproutRuntime.Light") {}

bool LightComponent::SetSettings(const LightSettings& settings) {
    const auto& color=settings.Color;
    if(settings.Type!=LightType::Directional || !std::isfinite(settings.Intensity) || settings.Intensity<0 ||
       !std::isfinite(color.R) || !std::isfinite(color.G) || !std::isfinite(color.B) || !std::isfinite(color.A) ||
       color.R<0 || color.G<0 || color.B<0 || color.A<0 || color.A>1 || !IsValidLightShadows(settings.Shadows)) return false;
    const auto& previous=_Settings.Color;
    if(settings.Type==_Settings.Type && settings.Enabled==_Settings.Enabled &&
       settings.Intensity==_Settings.Intensity && settings.CullingMask==_Settings.CullingMask &&
       color.R==previous.R && color.G==previous.G && color.B==previous.B && color.A==previous.A &&
       settings.Shadows==_Settings.Shadows) return true;
    _Settings=settings;
    NotifyComponentChanged(_GameObject,this);
    return true;
}

void LightComponent::RequestShadowRefresh() {
    ++_ShadowRefreshRevision;
    if(auto* scene=static_cast<Scene*>(_GameObject->GetScene()))scene->InvalidateVisual();
}
}

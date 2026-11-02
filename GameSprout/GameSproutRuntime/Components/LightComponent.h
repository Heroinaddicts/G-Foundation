#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class LightComponent final : public BuiltinComponent<ILightComponent> {
public:
    explicit LightComponent(IGameObject* owner);
    const LightSettings& GetSettings() const override { return _Settings; }
    bool SetSettings(const LightSettings& settings) override;
private:
    LightSettings _Settings;
};
}

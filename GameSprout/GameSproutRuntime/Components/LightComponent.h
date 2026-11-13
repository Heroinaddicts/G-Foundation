#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class LightComponent final : public BuiltinComponent<ILightComponent> {
public:
    explicit LightComponent(IGameObject* owner);
    const LightSettings& GetSettings() const override { return _Settings; }
    bool SetSettings(const LightSettings& settings) override;
    void RequestShadowRefresh() override;
    std::uint64_t GetShadowRefreshRevision() const { return _ShadowRefreshRevision; }
private:
    LightSettings _Settings;
    std::uint64_t _ShadowRefreshRevision=0;
};
}

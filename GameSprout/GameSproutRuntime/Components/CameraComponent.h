#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class CameraComponent final : public BuiltinComponent<ICameraComponent> {
public:
    explicit CameraComponent(IGameObject* owner);
    const CameraSettings& GetSettings() const override { return _Settings; }
    bool SetSettings(const CameraSettings& value) override;
    bool GetViewMatrix(Mat4& output) const override;
    bool GetProjectionMatrix(float aspect, Mat4& output) const override;

private:
    CameraSettings _Settings;
};
}

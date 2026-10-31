#pragma once
#include "BuiltinComponent.h"

namespace GameSproutRuntime {
class TransformComponent final : public BuiltinComponent<ITransformComponent> {
public:
    explicit TransformComponent(IGameObject* owner);
    Vector3 GetLocalPosition() const override { return _Position; }
    void SetLocalPosition(Vector3 value) override;
    Quaternion GetLocalRotation() const override { return _Rotation; }
    bool SetLocalRotation(Quaternion value) override;
    Vector3 GetLocalScale() const override { return _Scale; }
    void SetLocalScale(Vector3 value) override;
    Mat4 GetLocalMatrix() const override;
    Mat4 GetWorldMatrix() const override;
    bool SetLocalMatrix(const Mat4& value);

private:
    bool IsSceneRoot() const;
    Vector3 _Position;
    Quaternion _Rotation;
    Vector3 _Scale{1.0f, 1.0f, 1.0f};
};
}

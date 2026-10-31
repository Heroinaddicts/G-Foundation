#include "ImageComponent.h"
#include "ComponentFactory.h"
#include "RuntimeMath.h"
#include <cmath>

namespace GameSproutRuntime {
ImageComponent::ImageComponent(IGameObject* owner)
    : BuiltinComponent<IImageComponent>(owner, "GameSproutRuntime.Image") {}
void ImageComponent::SetTexture(const ResourceReference& value) {
    if (_Texture == value) return;
    _Texture = value;
    // 后续资源系统按引用加载，Renderer 再通过 GRHI 建立 GPU 资源。
    NotifyComponentChanged(_GameObject, this);
}
void ImageComponent::SetColor(Color value) {
    if (!RuntimeMath::IsFinite(value) || value.R < 0 || value.G < 0 || value.B < 0 || value.A < 0 || value.A > 1 ||
        (value.R == _Color.R && value.G == _Color.G && value.B == _Color.B && value.A == _Color.A)) return;
    _Color = value;
    NotifyComponentChanged(_GameObject, this);
}
void ImageComponent::SetUVRect(const Rect& value) {
    if (!std::isfinite(value.Position.X) || !std::isfinite(value.Position.Y) ||
        !std::isfinite(value.Size.X) || !std::isfinite(value.Size.Y) || value.Size.X < 0 || value.Size.Y < 0 ||
        (value.Position.X == _UV.Position.X && value.Position.Y == _UV.Position.Y &&
         value.Size.X == _UV.Size.X && value.Size.Y == _UV.Size.Y)) return;
    _UV = value;
    NotifyComponentChanged(_GameObject, this);
}
}

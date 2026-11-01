#pragma once
#include "GameSproutRuntimeApi.h"
#include <vector>
#include <string>

namespace InspectorValues {
struct Transform { GameSproutRuntime::Vector3 Position; GameSproutRuntime::Quaternion Rotation; GameSproutRuntime::Vector3 Scale; };
struct RectTransform { GameSproutRuntime::Vector2 Minimum, Maximum, Pivot, Position, Size; };
struct Canvas { GameSproutRuntime::CanvasRenderMode Mode; GameSproutRuntime::Vector2 Resolution; float Pixels; };
struct Image { GameSproutRuntime::ResourceReference Texture; GameSproutRuntime::Color Color; GameSproutRuntime::Rect UV; };
struct MeshRenderer { bool Visible; std::vector<GameSproutRuntime::ResourceReference> Materials; };
Transform Read(const GameSproutRuntime::ITransformComponent& component);
RectTransform Read(const GameSproutRuntime::IRectTransformComponent& component);
Canvas Read(const GameSproutRuntime::ICanvasComponent& component);
Image Read(const GameSproutRuntime::IImageComponent& component);
MeshRenderer Read(const GameSproutRuntime::IMeshRendererComponent& component);
GameSproutRuntime::ResourceReference Read(const GameSproutRuntime::IMeshComponent& component);
GameSproutRuntime::CameraSettings Read(const GameSproutRuntime::ICameraComponent& component);
#define INSPECTOR_VALUE_OPERATIONS(Value, Interface) \
    bool Validate(Value& value, std::string& error); \
    bool Equal(const Value& a, const Value& b); \
    bool Write(GameSproutRuntime::Interface& component, const Value& value, std::string& error);
INSPECTOR_VALUE_OPERATIONS(Transform, ITransformComponent)
INSPECTOR_VALUE_OPERATIONS(RectTransform, IRectTransformComponent)
INSPECTOR_VALUE_OPERATIONS(Canvas, ICanvasComponent)
INSPECTOR_VALUE_OPERATIONS(Image, IImageComponent)
INSPECTOR_VALUE_OPERATIONS(MeshRenderer, IMeshRendererComponent)
INSPECTOR_VALUE_OPERATIONS(GameSproutRuntime::ResourceReference, IMeshComponent)
INSPECTOR_VALUE_OPERATIONS(GameSproutRuntime::CameraSettings, ICameraComponent)
#undef INSPECTOR_VALUE_OPERATIONS
}

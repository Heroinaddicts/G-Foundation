#include "ComponentEditorValues.h"
#include <cmath>

namespace InspectorValues {
namespace {
bool Same(GameSproutRuntime::Vector2 a, GameSproutRuntime::Vector2 b) { return a.X == b.X && a.Y == b.Y; }
bool Same(GameSproutRuntime::Vector3 a, GameSproutRuntime::Vector3 b) { return a.X == b.X && a.Y == b.Y && a.Z == b.Z; }
bool Finite(GameSproutRuntime::Vector2 v) { return std::isfinite(v.X) && std::isfinite(v.Y); }
bool Finite(GameSproutRuntime::Vector3 v) { return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z); }
bool Unit(GameSproutRuntime::Vector2 v) { return Finite(v) && v.X >= 0 && v.X <= 1 && v.Y >= 0 && v.Y <= 1; }
bool Checked(bool success, std::string& error) { if (!success && error.empty()) error = "Runtime 拒绝了属性，已保留原值。"; return success; }
}
Transform Read(const GameSproutRuntime::ITransformComponent& c) { return {c.GetLocalPosition(), c.GetLocalRotation(), c.GetLocalScale()}; }
RectTransform Read(const GameSproutRuntime::IRectTransformComponent& c) { return {c.GetAnchorMin(), c.GetAnchorMax(), c.GetPivot(), c.GetAnchoredPosition(), c.GetSizeDelta()}; }
Canvas Read(const GameSproutRuntime::ICanvasComponent& c) { return {c.GetRenderMode(), c.GetReferenceResolution(), c.GetPixelsPerUnit()}; }
Image Read(const GameSproutRuntime::IImageComponent& c) { return {c.GetTexture(), c.GetColor(), c.GetUVRect()}; }
GameSproutRuntime::ResourceReference Read(const GameSproutRuntime::IMeshComponent& c) { return c.GetMesh(); }
GameSproutRuntime::CameraSettings Read(const GameSproutRuntime::ICameraComponent& c) { return c.GetSettings(); }
MeshRenderer Read(const GameSproutRuntime::IMeshRendererComponent& c) {
    MeshRenderer result{c.IsVisible(), {}};
    for (std::size_t slot = 0; slot < c.GetMaterialCount(); ++slot) result.Materials.push_back(c.GetMaterial(slot));
    return result;
}
bool Equal(const Transform& a, const Transform& b) {
    const auto& x = a.Rotation; const auto& y = b.Rotation;
    const bool same = x.X == y.X && x.Y == y.Y && x.Z == y.Z && x.W == y.W;
    const bool opposite = x.X == -y.X && x.Y == -y.Y && x.Z == -y.Z && x.W == -y.W;
    return Same(a.Position, b.Position) && Same(a.Scale, b.Scale) && (same || opposite);
}
bool Equal(const RectTransform& a, const RectTransform& b) { return Same(a.Minimum,b.Minimum) && Same(a.Maximum,b.Maximum) && Same(a.Pivot,b.Pivot) && Same(a.Position,b.Position) && Same(a.Size,b.Size); }
bool Equal(const Canvas& a, const Canvas& b) { return a.Mode == b.Mode && Same(a.Resolution,b.Resolution) && a.Pixels == b.Pixels; }
bool Equal(const Image& a, const Image& b) { return a.Texture == b.Texture && a.Color.R == b.Color.R && a.Color.G == b.Color.G && a.Color.B == b.Color.B && a.Color.A == b.Color.A && Same(a.UV.Position,b.UV.Position) && Same(a.UV.Size,b.UV.Size); }
bool Equal(const MeshRenderer& a, const MeshRenderer& b) { return a.Visible == b.Visible && a.Materials == b.Materials; }
bool Equal(const GameSproutRuntime::ResourceReference& a, const GameSproutRuntime::ResourceReference& b) { return a == b; }
bool Equal(const GameSproutRuntime::CameraSettings& a, const GameSproutRuntime::CameraSettings& b) { return a.Projection == b.Projection && a.VerticalFovRadians == b.VerticalFovRadians && a.OrthographicHeight == b.OrthographicHeight && a.NearPlane == b.NearPlane && a.FarPlane == b.FarPlane && a.CullingMask == b.CullingMask; }
bool Validate(Transform& v, std::string& error) {
    GameSproutRuntime::Quaternion unit;
    if (!Finite(v.Position) || !Finite(v.Scale) || !v.Rotation.TryNormalize(unit)) { error = "位置、缩放必须为有限数；旋转不能是零四元数。"; return false; }
    v.Rotation = unit;
    return true;
}
bool Validate(RectTransform& v, std::string& error) {
    if (!Unit(v.Minimum) || !Unit(v.Maximum) || !Unit(v.Pivot) || v.Minimum.X > v.Maximum.X || v.Minimum.Y > v.Maximum.Y || !Finite(v.Position) || !Finite(v.Size)) {
        error = "锚点与枢轴须在 0..1 范围，最小锚点不能超过最大锚点；其他值必须为有限数。"; return false;
    }
    return true;
}
bool Validate(Canvas& v, std::string& error) {
    if ((v.Mode != GameSproutRuntime::CanvasRenderMode::ScreenSpace && v.Mode != GameSproutRuntime::CanvasRenderMode::WorldSpace) || !Finite(v.Resolution) || v.Resolution.X <= 0 || v.Resolution.Y <= 0 || !std::isfinite(v.Pixels) || v.Pixels <= 0) {
        error = "Canvas 分辨率和每米布局单位必须为有限正数。"; return false;
    }
    return true;
}
bool Validate(GameSproutRuntime::ResourceReference& v, std::string& error) {
    if (v.Guid.empty()) { v.LocalID = 0; return true; }
    bool nonzero = false;
    if (v.Guid.size() == 32 && v.LocalID) {
        for (char digit : v.Guid) {
            if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F'))) { error = "资源 GUID 无效。"; return false; }
            nonzero = nonzero || digit != '0';
        }
        if (nonzero) return true;
    }
    error = "资源引用需要有效 GUID 与非零 LocalID。"; return false;
}
bool Validate(Image& v, std::string& error) {
    const auto& c = v.Color;
    if (!Validate(v.Texture,error)) return false;
    if (!std::isfinite(c.R) || !std::isfinite(c.G) || !std::isfinite(c.B) || !std::isfinite(c.A) || c.R < 0 || c.G < 0 || c.B < 0 || c.A < 0 || c.A > 1 || !Finite(v.UV.Position) || !Finite(v.UV.Size) || v.UV.Size.X < 0 || v.UV.Size.Y < 0) {
        error = "颜色 RGB 不能为负，Alpha 须在 0..1；UV 尺寸不能为负，全部数值必须有限。"; return false;
    }
    return true;
}
bool Validate(MeshRenderer& v, std::string& error) {
    if (v.Materials.size() > 1024) { error = "材质槽最多 1024 个。"; return false; }
    for (auto& material : v.Materials) if (!Validate(material,error)) return false;
    return true;
}
bool Validate(GameSproutRuntime::CameraSettings& v, std::string& error) {
    if ((v.Projection != GameSproutRuntime::CameraProjection::Perspective && v.Projection != GameSproutRuntime::CameraProjection::Orthographic) || !std::isfinite(v.VerticalFovRadians) || v.VerticalFovRadians <= 0 || v.VerticalFovRadians >= GameSproutRuntime::Pi || !std::isfinite(v.OrthographicHeight) || v.OrthographicHeight <= 0 || !std::isfinite(v.NearPlane) || !std::isfinite(v.FarPlane) || v.NearPlane < 0 || v.FarPlane <= v.NearPlane || (v.Projection == GameSproutRuntime::CameraProjection::Perspective && v.NearPlane <= 0)) {
        error = "相机 FOV 须在 0..180 度之间，远面须大于近面；透视近面和正交高度须为正。"; return false;
    }
    return true;
}
bool Write(GameSproutRuntime::ITransformComponent& c, const Transform& v, std::string& error) {
    if (c.GetGameObject()->GetScene() == c.GetGameObject()) { error = "场景根 Transform 固定为单位变换。"; return false; }
    if (!c.SetLocalRotation(v.Rotation)) return Checked(false,error);
    c.SetLocalPosition(v.Position); c.SetLocalScale(v.Scale);
    // Runtime 会再次归一化，旋转以单位四元数同向/反向等价验证。
    const auto current = Read(c);
    const float dot = current.Rotation.X*v.Rotation.X + current.Rotation.Y*v.Rotation.Y + current.Rotation.Z*v.Rotation.Z + current.Rotation.W*v.Rotation.W;
    return Checked(Same(current.Position,v.Position) && Same(current.Scale,v.Scale) && std::fabs(std::fabs(dot)-1.0f) < 0.00001f,error);
}
bool Write(GameSproutRuntime::IRectTransformComponent& c, const RectTransform& v, std::string& error) { c.SetAnchors(v.Minimum,v.Maximum); c.SetPivot(v.Pivot); c.SetAnchoredPosition(v.Position); c.SetSizeDelta(v.Size); return Checked(Equal(Read(c),v),error); }
bool Write(GameSproutRuntime::ICanvasComponent& c, const Canvas& v, std::string& error) { c.SetRenderMode(v.Mode); return Checked(c.SetReferenceResolution(v.Resolution) && c.SetPixelsPerUnit(v.Pixels) && Equal(Read(c),v),error); }
bool Write(GameSproutRuntime::IImageComponent& c, const Image& v, std::string& error) { c.SetTexture(v.Texture); c.SetColor(v.Color); c.SetUVRect(v.UV); return Checked(Equal(Read(c),v),error); }
bool Write(GameSproutRuntime::IMeshComponent& c, const GameSproutRuntime::ResourceReference& v, std::string& error) { c.SetMesh(v); return Checked(Equal(c.GetMesh(),v),error); }
bool Write(GameSproutRuntime::IMeshRendererComponent& c, const MeshRenderer& v, std::string& error) { if (!c.SetMaterials(v.Materials)) return Checked(false,error); c.SetVisible(v.Visible); return Checked(Equal(Read(c),v),error); }
bool Write(GameSproutRuntime::ICameraComponent& c, const GameSproutRuntime::CameraSettings& v, std::string& error) { return Checked(c.SetSettings(v) && Equal(c.GetSettings(),v),error); }
}

#pragma once
#include "RuntimeScriptModule.h"
#include "Math/TransformMath.h"
#include <algorithm>
#include <cmath>

// A project-owned camera behaviour. Camera rendering, input delivery and
// physics remain public Runtime services; there is no Editor dependency here.
class ThirdPersonCamera final : public GameSproutRuntime::ScriptBehaviour {
public:
    GameSproutRuntime::IGameObject* Target = nullptr;
    float Distance = 5.5f;              // Unobstructed distance, meters.
    float Height = 1.5f;                // Aim above Target's world origin, meters.
    float FollowSharpness = 12.0f;      // Exponential follow rate; 0 snaps.
    float MouseSensitivity = 0.18f;     // Degrees per logical mouse pixel.
    float Yaw = 0.0f;                   // 0 looks along Runtime forward (-Z).
    float Pitch = 18.0f;                // Positive looks down towards Target.
    float MinPitch = -25.0f;
    float MaxPitch = 70.0f;
    float MinDistance = 0.45f;           // Unobstructed minimum, never overrides a wall hit.
    float CollisionPadding = 0.25f;     // Camera clearance around the sampled rays, meters.
    uint32_t ObstacleMask = GameSproutRuntime::DefaultRaycastLayers;

protected:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        _HasPosition = false;
        _OrbitHeld = false;
        _TrackedObjectID = 0;
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        // Serialized Target and settings have been assigned at this stage.
        Place(runtime, 0.0f, true);
    }
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override {
        if (!runtime || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
        const auto& input = runtime->GetInputState();
        const auto orbit = input.GetMouseButton(GameSproutRuntime::InputMouseButton::Right);
        const bool held = input.Focused && orbit.Held;
        // Ignore the acquisition click's accumulated pre-click motion. Runtime
        // also clears MouseDelta and held input on focus loss/pause/stop.
        if (held && _OrbitHeld && !orbit.Pressed) {
            const float sensitivity = std::clamp(Finite(MouseSensitivity, 0.18f), 0.0f, 10.0f);
            Yaw = Finite(Yaw, 0.0f) + Finite(input.MouseDelta.X, 0.0f) * sensitivity;
            Pitch = Finite(Pitch, 18.0f) + Finite(input.MouseDelta.Y, 0.0f) * sensitivity;
        }
        _OrbitHeld = held;
        Place(runtime, deltaSeconds, false);
    }
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*, float) override {}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {
        _HasPosition = false;
        _OrbitHeld = false;
        _TrackedObjectID = 0;
    }

private:
    static float Finite(float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    }
    static GameSproutRuntime::Vector3 Point(const GameSproutRuntime::Mat4& matrix,
                                           GameSproutRuntime::Vector3 value) {
        const auto result = matrix * GameSproutRuntime::Vector4(value.X, value.Y, value.Z, 1.0f);
        return {result.X, result.Y, result.Z};
    }
    static GameSproutRuntime::Vector3 Direction(const GameSproutRuntime::Mat4& matrix,
                                               GameSproutRuntime::Vector3 value) {
        const auto result = matrix * GameSproutRuntime::Vector4(value.X, value.Y, value.Z, 0.0f);
        return {result.X, result.Y, result.Z};
    }
    bool WriteWorldPose(GameSproutRuntime::Vector3 position, GameSproutRuntime::Vector3 forward) {
        auto* transform = _GameObject->GetTransform();
        GameSproutRuntime::Mat4 parentInverse;
        if (auto* parent = _GameObject->Parent()) {
            if (!GameSproutRuntime::RuntimeMath::TryInverseAffine(parent->GetTransform()->GetWorldMatrix(), parentInverse)) return false;
        }
        const auto localEye = Point(parentInverse, position);
        const auto localForward = Direction(parentInverse, forward);
        const auto localUp = Direction(parentInverse, {0.0f, 1.0f, 0.0f});
        GameSproutRuntime::Mat4 localView, localCamera;
        if (!GameSproutRuntime::TryMakeLookAt(localEye, localEye + localForward, localUp, localView) ||
            !GameSproutRuntime::RuntimeMath::TryInverseAffine(localView, localCamera)) return false;
        GameSproutRuntime::Vector3 localPosition, localScale;
        GameSproutRuntime::Quaternion localRotation;
        if (!GameSproutRuntime::RuntimeMath::TryDecomposeTRS(localCamera, localPosition, localRotation, localScale)) return false;
        // Convert world position/aim into the actual parent's space. The sample
        // camera is a direct scene child, but reparenting never treats a world
        // position as local coordinates or applies the parent's rotation twice.
        if (!transform->SetLocalRotation(localRotation)) return false;
        transform->SetLocalPosition(localPosition);
        return true;
    }
    bool ClearDistance(GameSproutRuntime::IGameSproutRuntime* runtime,
                       GameSproutRuntime::ICameraComponent* camera,
                       GameSproutRuntime::Vector3 focus, GameSproutRuntime::Vector3 back,
                       float distance, float& allowed) const {
        GameSproutRuntime::PhysicsQuerySettings query;
        query.CollisionMask = ObstacleMask;
        query.IncludeTriggers = false;
        query.IgnoredObjectIDs.push_back(_GameObject->GetInstanceID());
        // Exclude the full character hierarchy, including independent child
        // rigidbodies; excluding only the root compound would not cover those.
        std::vector<GameSproutRuntime::IGameObject*> pending{Target};
        for (std::size_t index = 0; index < pending.size(); ++index) {
            auto* object = pending[index];
            query.IgnoredObjectIDs.push_back(object->GetInstanceID());
            for (std::size_t child = 0; child < object->GetChildCount(); ++child)
                if (auto* value = object->GetChild(child)) pending.push_back(value);
        }
        const float padding = std::max(std::max(0.0f, Finite(CollisionPadding, 0.25f)), camera->GetSettings().NearPlane);
        GameSproutRuntime::Vector3 right, up;
        if (!(-back).Cross({0.0f, 1.0f, 0.0f}).TryNormalize(right)) right = {1.0f, 0.0f, 0.0f};
        up = back.Cross(right);
        // A centre ray plus a clearance ring protects against side/corner
        // clipping as well as a wall directly between the character and camera.
        const GameSproutRuntime::Vector3 offsets[] = {
            {}, right * padding, -right * padding, up * padding, -up * padding
        };
        allowed = distance;
        for (const auto offset : offsets) {
            GameSproutRuntime::PhysicsRaycastHit hit;
            std::string error;
            if (runtime->Raycast(_GameObject->GetScene(), {focus + offset, back}, distance + padding, hit, query, error))
                allowed = std::min(allowed, std::max(0.0f, hit.Distance - padding));
            else if (!error.empty()) return false; // Do not advance through an unavailable physics world.
        }
        return true;
    }
    void Place(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds, bool snap) {
        if (!runtime || !Target || Target == _GameObject || Target->GetScene() != _GameObject->GetScene()) return;
        auto* camera = _GameObject->GetComponent<GameSproutRuntime::ICameraComponent>();
        if (!camera) return;
        const auto targetWorld = Target->GetTransform()->GetWorldMatrix();
        const GameSproutRuntime::Vector3 focus{targetWorld(0,3), targetWorld(1,3) + Finite(Height, 1.5f), targetWorld(2,3)};
        if (!GameSproutRuntime::RuntimeMath::IsFinite(focus)) return;
        const float low = std::clamp(std::min(Finite(MinPitch, -25.0f), Finite(MaxPitch, 70.0f)), -89.0f, 89.0f);
        const float high = std::clamp(std::max(Finite(MinPitch, -25.0f), Finite(MaxPitch, 70.0f)), -89.0f, 89.0f);
        Yaw = std::remainder(Finite(Yaw, 0.0f), 360.0f);
        Pitch = std::clamp(Finite(Pitch, 18.0f), low, high);
        const float yaw = GameSproutRuntime::DegreesToRadians(Yaw), pitch = GameSproutRuntime::DegreesToRadians(Pitch);
        const GameSproutRuntime::Vector3 forward{std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
        const float minimum = std::max(0.05f, Finite(MinDistance, 0.45f));
        const float desiredDistance = std::max(minimum, Finite(Distance, 5.5f));
        const auto desired = focus - forward * desiredDistance;
        if (_TrackedObjectID != Target->GetInstanceID()) { _HasPosition = false; _TrackedObjectID = Target->GetInstanceID(); }
        const float sharpness = std::max(0.0f, Finite(FollowSharpness, 12.0f));
        const float blend = snap || !_HasPosition || sharpness == 0.0f ? 1.0f : 1.0f - std::exp(-sharpness * deltaSeconds);
        const auto candidate = _HasPosition ? _Position + (desired - _Position) * blend : desired;
        GameSproutRuntime::Vector3 back;
        const auto offset = candidate - focus;
        if (!offset.TryNormalize(back)) back = -forward;
        const float distance = std::max(minimum, offset.Length());
        float allowed = distance;
        if (!ClearDistance(runtime, camera, focus, back, distance, allowed)) return;
        // Obstruction correction is immediate. Only the unobstructed recovery
        // is smoothed; clamping to MinDistance here could put the camera behind
        // a wall closer than the configured minimum.
        const auto position = focus + back * allowed;
        if (!WriteWorldPose(position, -back)) return;
        _Position = position;
        _HasPosition = true;
    }
    GameSproutRuntime::Vector3 _Position;
    uint64_t _TrackedObjectID = 0;
    bool _HasPosition = false;
    bool _OrbitHeld = false;
};
GAMESPROUT_SCRIPT(ThirdPersonCamera)

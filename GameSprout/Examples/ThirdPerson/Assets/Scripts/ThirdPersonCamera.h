#pragma once
#include "RuntimeScriptModule.h"

// A project-owned camera behaviour. Camera rendering, input delivery and
// physics remain public Runtime services; there is no Editor dependency here.
class ThirdPersonCamera final : public GameSproutRuntime::ScriptBehaviour {
public:
    GameSproutRuntime::IGameObject* Target = nullptr;
    float Distance = 5.5f;              // Unobstructed distance, meters.
    float Height = 1.5f;                // Aim above Target's world origin, meters.
    float FollowSharpness = 12.0f;      // Exponential follow rate; 0 snaps.
    float MouseSensitivity = 0.18f;     // Degrees per logical mouse pixel.
    float Yaw = 0.0f;                   // 0 looks along Unity-style world forward (+Z).
    float Pitch = 18.0f;                // Positive looks down towards Target.
    float MinPitch = -25.0f;
    float MaxPitch = 70.0f;
    float MinDistance = 0.45f;           // Unobstructed minimum, never overrides a wall hit.
    float CollisionPadding = 0.25f;     // Camera clearance around the sampled rays, meters.
    uint32_t ObstacleMask = GameSproutRuntime::DefaultRaycastLayers;

protected:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override;
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* runtime) override;
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override;

private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<ThirdPersonCamera>;

    explicit ThirdPersonCamera(GameSproutRuntime::IGameSproutRuntime* runtime);

    static float Finite(float value, float fallback);
    static GameSproutRuntime::Vector3 Point(const GameSproutRuntime::Mat4& matrix,
                                           GameSproutRuntime::Vector3 value);
    static GameSproutRuntime::Vector3 Direction(const GameSproutRuntime::Mat4& matrix,
                                               GameSproutRuntime::Vector3 value);
    bool WriteWorldPose(GameSproutRuntime::Vector3 position, GameSproutRuntime::Vector3 forward);
    bool ClearDistance(GameSproutRuntime::ICameraComponent* camera,
                       GameSproutRuntime::Vector3 focus, GameSproutRuntime::Vector3 back,
                       float distance, float& allowed) const;
    void Place(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds, bool snap);
    GameSproutRuntime::IGameSproutRuntime* const _Runtime;
    GameSproutRuntime::Vector3 _Position;
    uint64_t _TrackedObjectID = 0;
    bool _HasPosition = false;
    bool _OrbitHeld = false;
};
GAMESPROUT_SCRIPT(ThirdPersonCamera)

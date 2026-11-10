#pragma once
#include "RuntimeScriptModule.h"

// Project gameplay code: Runtime owns input, physics, animation and all objects.
// The physical capsule stays upright; Visual turns independently around world Y.
class ThirdPersonCharacter final : public GameSproutRuntime::ScriptBehaviour {
public:
    GameSproutRuntime::IRigidbodyComponent* Body = nullptr;
    GameSproutRuntime::ICapsuleColliderComponent* Collider = nullptr;
    GameSproutRuntime::IGameObject* ViewCamera = nullptr;
    GameSproutRuntime::IGameObject* Visual = nullptr;
    GameSproutRuntime::IAnimatorComponent* Animator = nullptr;
    GameSproutRuntime::ResourceReference IdleClip;
    GameSproutRuntime::ResourceReference WalkClip;
    GameSproutRuntime::ResourceReference RunClip;
    GameSproutRuntime::ResourceReference JumpClip;
    float WalkSpeed = 3.0f;
    float RunSpeed = 6.0f;
    float JumpSpeed = 5.5f;
    float GroundProbe = 0.18f;
    float MaxSlopeDegrees = 50.0f;
    float TurnSharpness = 14.0f;
    uint32_t GroundMask = GameSproutRuntime::DefaultRaycastLayers;

protected:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override;
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override;
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override;
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float fixedDeltaSeconds) override;
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override;

private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<ThirdPersonCharacter>;

    explicit ThirdPersonCharacter(GameSproutRuntime::IGameSproutRuntime* runtime);

    enum class Motion { None,Idle,Walk,Run,Jump };
    static float Finite(float value,float fallback);
    static GameSproutRuntime::Vector3 Point(const GameSproutRuntime::Mat4& matrix,GameSproutRuntime::Vector3 value);
    GameSproutRuntime::Vector3 MovementDirection() const;
    bool Grounded() const;
    void Turn(float deltaSeconds);
    void Animate();
    GameSproutRuntime::IGameSproutRuntime* const _Runtime;
    GameSproutRuntime::Vector2 _Move;
    Motion _Motion=Motion::None;
    float _Heading=0.0f,_TargetHeading=0.0f;
    bool _Run=false,_JumpRequested=false,_Grounded=false,_HasHeading=false;
};
GAMESPROUT_SCRIPT(ThirdPersonCharacter)

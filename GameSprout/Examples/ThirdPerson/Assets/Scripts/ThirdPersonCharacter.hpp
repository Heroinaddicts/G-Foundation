#pragma once
#include "RuntimeScriptModule.h"
#include "Math/TransformMath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        _Motion = Motion::None;
        _Move = {};
        _Run = false;
        _JumpRequested = false;
        _Grounded = false;
        _HasHeading = false;
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        if (!Body) Body = _GameObject->GetComponent<GameSproutRuntime::IRigidbodyComponent>();
        if (!Collider) Collider = _GameObject->GetComponent<GameSproutRuntime::ICapsuleColliderComponent>();
        if (!Body || !Collider || Body->GetGameObject() != _GameObject ||
            Collider->GetAttachedRigidbody() != Body || Collider->GetShape().Axis != GameSproutRuntime::PhysicsAxis::Y)
            throw std::runtime_error("ThirdPersonCharacter requires its own Dynamic Rigidbody and a Y-axis CapsuleCollider");
        if (!Body->IsEnabled() || Body->GetMotionType() != GameSproutRuntime::PhysicsMotionType::Dynamic)
            throw std::runtime_error("ThirdPersonCharacter requires an enabled Dynamic Rigidbody");
        if (!Body->SetConstraints(Body->GetConstraints() | static_cast<uint32_t>(GameSproutRuntime::PhysicsConstraint::FreezeRotation)))
            throw std::runtime_error("Cannot keep the character capsule upright");
        if (Animator) Animator->SetRootMotionMode(GameSproutRuntime::RootMotionMode::Disabled);
    }
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime, float deltaSeconds) override {
        if (!runtime || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
        const auto& input = runtime->GetInputState();
        if (!input.Focused) {
            _Move = {};
            _Run = false;
            _JumpRequested = false;
        } else {
            const float x = (input.GetKey(GameSproutRuntime::InputKey::D).Held ? 1.0f : 0.0f) -
                            (input.GetKey(GameSproutRuntime::InputKey::A).Held ? 1.0f : 0.0f);
            const float y = (input.GetKey(GameSproutRuntime::InputKey::W).Held ? 1.0f : 0.0f) -
                            (input.GetKey(GameSproutRuntime::InputKey::S).Held ? 1.0f : 0.0f);
            _Move = {x,y};
            _Run = input.GetKey(GameSproutRuntime::InputKey::Shift).Held;
            // Pressed lasts one rendered input frame, which can contain several
            // fixed steps. Latch once here; consume once in FixedUpdate.
            _JumpRequested = _JumpRequested || input.GetKey(GameSproutRuntime::InputKey::Space).Pressed;
        }
        Turn(deltaSeconds);
        Animate();
    }
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime* runtime, float fixedDeltaSeconds) override {
        if (!runtime || !Body || !Collider || !std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0f) return;
        if (!Body->IsEnabled() || Body->GetMotionType() != GameSproutRuntime::PhysicsMotionType::Dynamic) return;
        const bool focused = runtime->GetInputState().Focused;
        if (!focused) {_Move={};_Run=false;_JumpRequested=false;}
        auto velocity = Body->GetLinearVelocity();
        _Grounded = Grounded(runtime) && velocity.Y <= 0.5f;
        const auto direction = MovementDirection();
        const float speed = std::max(0.0f, Finite(_Run ? RunSpeed : WalkSpeed, 0.0f));
        velocity.X = direction.X * speed;
        velocity.Z = direction.Z * speed;
        if (focused && _JumpRequested && _Grounded) {
            velocity.Y = std::max(0.0f, Finite(JumpSpeed, 5.5f));
            _Grounded = false;
        }
        _JumpRequested = false; // Airborne presses cannot become delayed auto-jumps.
        Body->SetLinearVelocity(velocity);
        if (direction.Dot(direction) > 0.0f) {
            _TargetHeading = std::atan2(-direction.X, -direction.Z);
            if (!_HasHeading) {_Heading=_TargetHeading;_HasHeading=true;}
        }
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {
        _Move={};_JumpRequested=false;_Grounded=false;_HasHeading=false;
        if (Animator) Animator->StopClip();
    }

private:
    enum class Motion { None,Idle,Walk,Run,Jump };
    static float Finite(float value,float fallback) {return std::isfinite(value)?value:fallback;}
    static GameSproutRuntime::Vector3 Point(const GameSproutRuntime::Mat4& matrix,GameSproutRuntime::Vector3 value) {
        const auto transformed=matrix*GameSproutRuntime::Vector4(value.X,value.Y,value.Z,1.0f);
        return {transformed.X,transformed.Y,transformed.Z};
    }
    GameSproutRuntime::Vector3 MovementDirection() const {
        GameSproutRuntime::Vector3 forward{0.0f,0.0f,-1.0f};
        if (ViewCamera && ViewCamera->GetScene()==_GameObject->GetScene()) {
            const auto matrix=ViewCamera->GetTransform()->GetWorldMatrix();
            const GameSproutRuntime::Vector3 projected{-matrix(0,2),0.0f,-matrix(2,2)};
            projected.TryNormalize(forward);
        }
        const auto right=forward.Cross({0.0f,1.0f,0.0f});
        auto direction=right*_Move.X+forward*_Move.Y;
        GameSproutRuntime::Vector3 normalized;
        return direction.TryNormalize(normalized)?normalized:GameSproutRuntime::Vector3{};
    }
    bool Grounded(GameSproutRuntime::IGameSproutRuntime* runtime) const {
        if (!Collider->GetColliderSettings().Enabled || Collider->GetColliderSettings().IsTrigger) return false;
        const auto shape=Collider->GetShape();
        const auto world=Collider->GetGameObject()->GetTransform()->GetWorldMatrix();
        const auto center=Point(world,Collider->GetColliderSettings().Center);
        // The upright sample uses a Y capsule. Its world vertical extent scales
        // with the owner's world Y basis rather than assuming local == world.
        const float halfHeight=shape.Height*0.5f*GameSproutRuntime::Vector3{world(0,1),world(1,1),world(2,1)}.Length();
        GameSproutRuntime::PhysicsQuerySettings query;
        query.CollisionMask=GroundMask;
        query.IncludeTriggers=false;
        std::vector<GameSproutRuntime::IGameObject*> nodes{_GameObject};
        for (std::size_t index=0;index<nodes.size();++index) {
            auto* node=nodes[index];query.IgnoredObjectIDs.push_back(node->GetInstanceID());
            for (std::size_t child=0;child<node->GetChildCount();++child)if(auto* item=node->GetChild(child))nodes.push_back(item);
        }
        GameSproutRuntime::PhysicsRaycastHit hit;std::string error;
        const float probe=std::clamp(Finite(GroundProbe,0.18f),0.01f,1.0f);
        if (!runtime->Raycast(_GameObject->GetScene(),{center,{0.0f,-1.0f,0.0f}},halfHeight+probe,hit,query,error))return false;
        const float slope=std::clamp(Finite(MaxSlopeDegrees,50.0f),0.0f,85.0f);
        return hit.Normal.Y>=std::cos(GameSproutRuntime::DegreesToRadians(slope));
    }
    void Turn(float deltaSeconds) {
        if (!Visual || Visual==_GameObject || !_HasHeading || Visual->GetScene()!=_GameObject->GetScene()) return;
        const float blend=1.0f-std::exp(-std::max(0.0f,Finite(TurnSharpness,14.0f))*deltaSeconds);
        _Heading+=std::remainder(_TargetHeading-_Heading,2.0f*GameSproutRuntime::Pi)*blend;
        GameSproutRuntime::Mat4 parentInverse;
        if (auto* parent=Visual->Parent())
            if (!GameSproutRuntime::RuntimeMath::TryInverseAffine(parent->GetTransform()->GetWorldMatrix(),parentInverse))return;
        const GameSproutRuntime::Vector3 worldForward{-std::sin(_Heading),0.0f,-std::cos(_Heading)};
        const auto transformed=parentInverse*GameSproutRuntime::Vector4(worldForward.X,worldForward.Y,worldForward.Z,0.0f);
        const auto up=parentInverse*GameSproutRuntime::Vector4(0.0f,1.0f,0.0f,0.0f);
        GameSproutRuntime::Mat4 view,pose;
        if (!GameSproutRuntime::TryMakeLookAt({}, {transformed.X,transformed.Y,transformed.Z},{up.X,up.Y,up.Z},view) ||
            !GameSproutRuntime::RuntimeMath::TryInverseAffine(view,pose))return;
        GameSproutRuntime::Vector3 position,scale;GameSproutRuntime::Quaternion rotation;
        if(GameSproutRuntime::RuntimeMath::TryDecomposeTRS(pose,position,rotation,scale))Visual->GetTransform()->SetLocalRotation(rotation);
    }
    void Animate() {
        if (!Animator) return;
        const Motion motion=!_Grounded?Motion::Jump:(_Move.X!=0.0f||_Move.Y!=0.0f)?(_Run?Motion::Run:Motion::Walk):Motion::Idle;
        GameSproutRuntime::ResourceReference clip;
        if(motion==Motion::Jump)clip=JumpClip;
        else if(motion==Motion::Run)clip=RunClip.Guid.empty()?WalkClip:RunClip;
        else if(motion==Motion::Walk)clip=WalkClip;
        else clip=IdleClip;
        if(clip.Guid.empty())clip=IdleClip;
        if(clip.Guid.empty())return;
        const auto current=Animator->GetClipPlaybackState();
        if(_Motion!=motion||!current.Active||current.Clip!=clip) {
            if(Animator->PlayClip(clip,0.0f,1.0f,motion!=Motion::Jump))_Motion=motion;
        }
    }
    GameSproutRuntime::Vector2 _Move;
    Motion _Motion=Motion::None;
    float _Heading=0.0f,_TargetHeading=0.0f;
    bool _Run=false,_JumpRequested=false,_Grounded=false,_HasHeading=false;
};
GAMESPROUT_SCRIPT(ThirdPersonCharacter)

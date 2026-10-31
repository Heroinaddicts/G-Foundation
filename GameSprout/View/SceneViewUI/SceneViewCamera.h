#pragma once
#include "ISceneViewUI.h"
#include "Math/Projection.h"

class SceneViewCamera {
public:
    SceneViewCamera();
    const SceneViewCameraState& GetState() const {return _State;}
    bool SetState(const SceneViewCameraState& state);
    void Reset();
    void Look(float horizontalPixels,float verticalPixels,bool orbit);
    void Pan(float horizontalPixels,float verticalPixels,float viewportHeight);
    void Dolly(float wheelSteps);
    void Move(float forward,float right,float upward,bool accelerated,float deltaSeconds);
    bool Focus(GameSproutRuntime::Vector3 center,float radius,float aspect = 1.0f);
    GameSproutRuntime::Vector3 Forward() const;
    GameSproutRuntime::Mat4 ViewMatrix() const;
private:
    void UpdatePivot();
    SceneViewCameraState _State;
};

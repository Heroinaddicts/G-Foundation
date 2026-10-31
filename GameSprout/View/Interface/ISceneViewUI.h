#ifndef __ISceneViewUI_h__
#define __ISceneViewUI_h__

#include "GameSproutEditorApi.h"
#include "Math/Vector3.h"

// 每个 Scene View 私有的编辑器观察状态，不是场景节点、资产或 Game 主相机。
struct SceneViewCameraState {
    GameSproutRuntime::Vector3 Position{6,4,8};
    GameSproutRuntime::Vector3 OrbitPivot;
    float YawDegrees=-36.8699f;
    float PitchDegrees=-21.8014f;
    float OrbitDistance=10.77033f;
    float VerticalFovDegrees=60;
    float NearPlane=0.05f;
    float FarPlane=10000;
    float MoveSpeed=6;
};

class ISceneViewUI : public GameSproutEditor::IGameSproutView {
public:
    virtual ~ISceneViewUI() {}
    virtual SceneViewCameraState GetCameraState() const = 0;
    virtual bool SetCameraState(const SceneViewCameraState& state) = 0;
    virtual void ResetCamera() = 0;
    virtual bool FocusSelection() = 0;
    virtual bool IsRenderingAvailable() const = 0;
    virtual std::string GetRenderError() const = 0;
};

#endif //__ISceneViewUI_h__

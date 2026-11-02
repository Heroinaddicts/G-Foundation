#pragma once
#include "ISceneTool.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <map>
class SceneViewport;

class SceneToolRouter final : public ISceneListener,public ISelectionListener {
public:
    explicit SceneToolRouter(SceneViewport& viewport):_Viewport(viewport) {}
    ~SceneToolRouter();
    void Bind(ISceneManager*,ISelectionManager*);
    void Shutdown();
    bool Register(const void*,ISceneTool*);
    bool Activate(ISceneTool*);
    void Deactivate(ISceneTool* tool=nullptr);
    void Unregister(const void*);
    ISceneTool* Active() const { return _Active; }
    bool Dragging() const { return _Active&&_Active->IsSceneToolDragging(); }
    void CancelDrag();
    void Draw(GameSproutRuntime::IScene*,GameSproutRuntime::DebugDrawList&);
    bool Press(GameSproutRuntime::Vector2,bool shift,bool alt,bool snap);
    bool Move(GameSproutRuntime::Vector2,bool shift,bool alt,bool snap);
    bool Release(GameSproutRuntime::Vector2,bool shift,bool alt,bool snap);
    void SetSnap(bool enabled,float value) { _Snap=enabled;_SnapDistance=value; }
    void OnActiveSceneChanged() override { Deactivate(); }
    void OnSceneStateChanged() override {}
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>& values) override { if(!values.empty())Deactivate(); }
    void OnSceneObjectSelectionChanged(const SceneObjectSelection&) override { Deactivate(); }
private:
    bool Context(SceneToolContext&) const;
    SceneToolPointer Pointer(GameSproutRuntime::Vector2,bool,bool,bool) const;
    SceneViewport& _Viewport;
    ISceneManager* _Scenes=nullptr;
    ISelectionManager* _Selection=nullptr;
    bool _SceneSubscribed=false,_SelectionSubscribed=false;
    std::map<ISceneTool*,const void*> _Tools;
    ISceneTool* _Active=nullptr;
    float _SnapDistance=1;
    bool _Snap=false;
};

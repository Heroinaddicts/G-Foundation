#pragma once
#include "GameObject.h"
#include <deque>

namespace GameSproutRuntime {
class Scene final : public GameObjectBase<IScene> {
public:
    Scene(Runtime& runtime, const std::string& name, std::uint64_t id)
        : GameObjectBase<IScene>(runtime,nullptr,name,id) { State().SceneOwner = this; }
    IGameObject* CreateGameObject(const std::string& name, IGameObject* parent = nullptr) override;
    bool DestroyGameObject(IGameObject* object) override;
    void SetSimulationEnabled(bool enabled) override;
    bool IsSimulationEnabled() const override;
    bool SetMainCamera(ICameraComponent* camera) override;
    ICameraComponent* GetMainCamera() const override;
    const EnvironmentLightingSettings& GetEnvironmentLighting() const override { return _EnvironmentLighting; }
    bool SetEnvironmentLighting(const EnvironmentLightingSettings&) override;
    // Runtime-internal snapshot invalidation. Changes are visible before queued
    // listener dispatch, including direct Set* -> Render calls in editor/tests.
    std::uint64_t GetRenderRevision() const {return _RenderRevision;}
    void InvalidateRenderSnapshot(std::uint64_t transformObjectID=0);
    void InvalidateVisual() { _VisualPending = true; }
    std::uint64_t GetUIRevision() const { return _UIRevision; }
    void InvalidateUI() { ++_UIRevision; _VisualPending = true; }
    // False if another kind of change or a journal gap requires a full snapshot.
    bool GetRenderTransformChanges(std::uint64_t after,std::vector<std::uint64_t>& changes) const;
    bool TakeVisualInvalidation() {const bool pending=_VisualPending;_VisualPending=false;return pending;}
    std::uint64_t GetAuthoringRevision() const override {return _AuthoringRevision;}
    bool GetAuthoringChanges(std::uint64_t after,std::vector<SceneAuthoringChange>& changes) const override {
        changes.clear();
        if(after>_AuthoringRevision||_AuthoringRevision-after>_AuthoringChanges.size())return false;
        const auto start=_AuthoringChanges.size()-static_cast<std::size_t>(_AuthoringRevision-after);
        changes.reserve(_AuthoringChanges.size()-start);
        for(auto index=start;index<_AuthoringChanges.size();++index)changes.push_back(_AuthoringChanges[index]);
        return true;
    }
    void RecordAuthoringChange(std::uint64_t objectID,SceneNodeEventType type,const char* component) {
        if(type==SceneNodeEventType::OperationFailed)return;
        ++_AuthoringRevision;
        try {
            _AuthoringChanges.push_back({objectID,type,component?component:""});
            if(_AuthoringChanges.size()>2048)_AuthoringChanges.pop_front();
        } catch(...) {_AuthoringChanges.clear();}
    }
    void ClearMainCameraForNode(std::uint64_t id);
    bool BindObjectIdentity(IGameObject* object,std::uint64_t fileID,std::string& error) override {
        auto* node=State().Owner.Find(object);
        if(!State().Owner.IsInScene(node) || node->SceneOwner!=this) {error="Object belongs to another scene";return false;}
        return State().Owner.BindSceneObjectIdentity(object,fileID,error);
    }
    bool SetComponentIdentities(const std::vector<SceneComponentIdentity>& values,std::string& error) override {
        return State().Owner.SetSceneComponentIdentities(this,values,error);
    }
    bool BindObjectIdentities(const std::vector<SceneObjectIdentity>& identities,std::string& error) override {
        return State().Owner.BindSceneObjectIdentities(this,identities,error);
    }
    bool BindAuthoringIdentities(const std::vector<SceneObjectIdentity>& objects,
        const std::vector<SceneComponentBinding>& components,std::string& error) override {
        return State().Owner.BindAuthoringIdentities(this,objects,components,error);
    }
private:
    std::uint64_t _MainCameraID = 0;
    bool _SimulationEnabled = true;
    EnvironmentLightingSettings _EnvironmentLighting;
    std::uint64_t _RenderRevision=1;
    std::uint64_t _UIRevision=1;
    std::deque<std::uint64_t> _RenderTransformChanges;
    std::uint64_t _AuthoringRevision=1;
    std::deque<SceneAuthoringChange> _AuthoringChanges;
    bool _VisualPending=false;
};
}

#pragma once
#include "GameObject.h"

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
private:
    std::uint64_t _MainCameraID = 0;
    bool _SimulationEnabled = true;
};
}

#pragma once
#include "GameObject.h"

namespace GameSproutRuntime {
class Scene final : public GameObjectBase<IScene> {
public:
    Scene(Runtime& runtime, const std::string& name, std::uint64_t id)
        : GameObjectBase<IScene>(runtime,nullptr,name,id) { State().SceneOwner = this; }
    IGameObject* CreateGameObject(const std::string& name, IGameObject* parent = nullptr) override;
    bool DestroyGameObject(IGameObject* object) override;
    bool SetMainCamera(ICameraComponent* camera) override;
    ICameraComponent* GetMainCamera() const override;
    void ClearMainCameraForNode(std::uint64_t id);
private:
    std::uint64_t _MainCameraID = 0;
};
}

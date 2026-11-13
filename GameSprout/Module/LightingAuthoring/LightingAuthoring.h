#pragma once
#include "Header.h"
#include "IPlayMode.h"
#include <map>
class ISelectionManager;
class IPlayMode;
class IRuntimeHost;
class QThread;

class LightingAuthoring final : public ILightingAuthoring, public ISceneListener,
    public IPlayModeListener, public GameSproutRuntime::IRuntimeListener {
public:
    ~LightingAuthoring() override;
    bool CanCreate(std::uint64_t,std::uint64_t,std::string&) const override;
    bool CreateDirectionalLight(std::uint64_t,std::uint64_t,std::uint64_t&,std::string&) override;
    bool ReadLight(const ObjectEditTarget&,GameSproutRuntime::LightSettings&,std::string&) const override;
    bool SetLight(const ObjectEditTarget&,const GameSproutRuntime::LightSettings&,std::string&) override;
    bool RequestShadowRefresh(const ObjectEditTarget&,std::string&) override;
    bool ReadEnvironment(EnvironmentLightingSnapshot&,std::string&) const override;
    bool SetEnvironment(std::uint64_t,const GameSproutRuntime::EnvironmentLightingSettings&,std::string&) override;
    bool RegisterEnvironmentListener(ILightingEnvironmentListener*) override;
    void UnregisterEnvironmentListener(ILightingEnvironmentListener*) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    GameSproutRuntime::IGameObject* Parent(std::uint64_t) const;
    GameSproutRuntime::ILightComponent* Resolve(const ObjectEditTarget&) const;
    ISceneManager* _Scenes=nullptr;
    IObjectEditing* _Objects=nullptr;
    ISelectionManager* _Selection=nullptr;
    IPlayMode* _Play=nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr;
    QThread* _OwnerThread=nullptr;
    void PublishEnvironment();
    bool _SceneSubscribed=false,_PlaySubscribed=false,_RuntimeSubscribed=false,_NotifyingEnvironment=false;
    bool _HaveEnvironmentSnapshot=false;
    EnvironmentLightingSnapshot _LastEnvironmentSnapshot;
    std::uint64_t _NextEnvironmentListener=0;
    std::map<std::uint64_t,ILightingEnvironmentListener*> _EnvironmentListeners;
};

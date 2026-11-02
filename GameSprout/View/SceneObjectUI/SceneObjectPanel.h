#pragma once
#include "InspectorPanel.h"
#include "IPlayMode.h"
#include "IProjectSettings.h"
namespace Ui { class SceneObjectPanelForm; }
class SceneObjectPanel final : public InspectorPanel, public IPlayModeListener, public IProjectSettingsListener {
public:
    SceneObjectPanel(const InspectorServices&, QWidget*);
    ~SceneObjectPanel() override;
    bool Load(const InspectorTarget&, bool, std::string&) override;
    InspectorPresentation Describe() const override;
    void Deactivate() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void OnProjectSettingsChanged(const GameSproutRuntime::RuntimeProjectSettings&,std::uint64_t) override;
    void SetDebugMode(bool) override;
    void SetLocked(bool) override;
    QWidget* SceneObjectHeaderHost() const override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
private:
    Ui::SceneObjectPanelForm* _Ui = nullptr;
    InspectorTarget _Target;
    IPlayMode* _PlayMode=nullptr;
    IProjectSettings* _ProjectSettings=nullptr;
    bool _SettingsSubscribed=false;
    bool _PlaySubscribed=false;
    bool _ReadOnly=false;
};

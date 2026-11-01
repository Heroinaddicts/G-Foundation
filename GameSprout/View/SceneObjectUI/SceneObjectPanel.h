#pragma once
#include "InspectorPanel.h"
namespace Ui { class SceneObjectPanelForm; }
class SceneObjectPanel final : public InspectorPanel {
public:
    SceneObjectPanel(const InspectorServices&, QWidget*);
    ~SceneObjectPanel() override;
    bool Load(const InspectorTarget&, bool, std::string&) override;
    InspectorPresentation Describe() const override;
    void Deactivate() override;
    void SetDebugMode(bool) override;
    void SetLocked(bool) override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
private:
    Ui::SceneObjectPanelForm* _Ui = nullptr;
    InspectorTarget _Target;
};

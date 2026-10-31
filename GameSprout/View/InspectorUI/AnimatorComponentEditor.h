#pragma once
#include "ComponentEditor.h"
#include <deque>
namespace Ui {class AnimatorComponentEditorForm;}
class AnimatorComponentEditor final : public ComponentEditor {
public:
    explicit AnimatorComponentEditor(QWidget* parent);
    ~AnimatorComponentEditor() override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
protected:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent&) override;
private:
    void CommitSettings();
    void RefreshPlayback();
    void RefreshLayerStates();
    void RefreshNotifications();
    GameSproutRuntime::IAnimatorComponent* Animator() const;
    Ui::AnimatorComponentEditorForm* _Ui=nullptr;
    GameSproutRuntime::ResourceReference _LoadedController;
    bool _HasController=false;
    GameSproutRuntime::AnimatorControllerData _ControllerData;
    std::uint64_t _ControllerRevision=0;
    std::deque<GameSproutRuntime::AnimationNotification> _Notifications;
    bool _NotificationsChanged=false;
};

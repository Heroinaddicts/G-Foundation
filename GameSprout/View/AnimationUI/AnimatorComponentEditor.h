#pragma once
#include "ComponentEditor.h"
#include "IRuntimeHost.h"
#include <deque>
namespace Ui {class AnimatorComponentEditorForm;}
class AnimatorComponentEditor final : public ComponentEditor, public IRuntimeFrameListener {
public:
    explicit AnimatorComponentEditor(QWidget* parent);
    ~AnimatorComponentEditor() override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
    void OnRuntimeFrame(const RuntimeFrameEvent&) override;
    void OnSimulationPlayingChanged(bool) override;
protected:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent&) override;
private:
    void CommitSettings();
    void RefreshPlayback(bool refreshResources = true);
    void BindFrameEvents();
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
    IRuntimeHost* _Host=nullptr;
    bool _FrameSubscribed=false;
    bool _SimulationPlaying=false;
    bool _FollowControllerPlayback=false;
};

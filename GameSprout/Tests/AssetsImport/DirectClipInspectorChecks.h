#pragma once
#include <QSlider>

static void CheckDirectClipInspector(ISceneManager* scenes,GameSproutRuntime::IGameObject* character,
    GameSproutRuntime::IGameObject* bone,QWidget* form,const QDir& root,
    const std::function<void()>& tick,const std::function<void(const std::function<bool()>&)>& pump,GameSproutRuntime::IGameSproutRuntime* runtime,IAnimationPreview* preview) {

    auto* animator=character->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    const auto objectID=character->GetInstanceID();
    const auto controller=animator->GetController(),defaultClip=animator->GetDefaultClip();
    const QByteArray savedBefore=ReadFile(root.filePath("Assets/Scenes/AnimatorInspector.scene"));
    auto* mode=form->findChild<QComboBox*>("previewModeCombo");
    auto* direct=form->findChild<QWidget*>("clipPreviewEditor");
    Check(mode&&direct&&mode->currentIndex()==0&&direct->isVisible(),"Inspector must open in direct Clip preview mode");
    auto* play=direct->findChild<QPushButton*>("clipPlayButton");
    auto* pause=direct->findChild<QPushButton*>("clipPauseButton");
    auto* stop=direct->findChild<QPushButton*>("clipStopButton");
    auto* seek=direct->findChild<QPushButton*>("clipSeekButton");
    auto* seconds=direct->findChild<QDoubleSpinBox*>("clipSeekSpin");
    auto* speed=direct->findChild<QDoubleSpinBox*>("clipSpeedSpin");
    auto* loop=direct->findChild<QCheckBox*>("clipLoopCheck");
    auto* slider=direct->findChild<QSlider*>("clipTimeSlider");
    auto* status=direct->findChild<QLabel*>("clipPlaybackLabel");
    Check(play&&pause&&stop&&seek&&seconds&&speed&&loop&&slider&&status
        &&direct->findChild<QWidget*>("directClipResource")&&direct->findChild<QComboBox*>("relatedClipCombo"),
        "Direct Clip preview Designer controls missing from the actual Inspector");

    // This condition would pull a manually selected Run state back to Idle.
    // Direct Clip evaluation must not execute that transition or mutate its parameter.
    Check(animator->SetFloat("Speed",0),"Cannot set direct-preview transition fixture");
    const auto controllerBefore=animator->GetPlaybackState();
    struct Notifications final : GameSproutRuntime::IRuntimeListener {
        std::uint64_t ObjectID=0;
        int Count=0;
        void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {}
        void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) override {
            if(event.ObjectID==ObjectID)++Count;
        }
    } notifications;
    notifications.ObjectID=objectID;
    Check(runtime->RegisterListener(&notifications),"Cannot subscribe direct Clip preview notification probe");
    struct ListenerScope {
        GameSproutRuntime::IGameSproutRuntime* Runtime;
        Notifications* Listener;
        ~ListenerScope(){Runtime->UnregisterListener(Listener);}
    } listenerScope{runtime,&notifications};
    const GameSproutRuntime::ResourceReference runClip{defaultClip.Guid,2};
    std::string error;
    Check(preview->PlayAnimationClip(objectID,runClip,0,1,true,error),error);
    pump([&]{return pause->isEnabled()&&preview->GetAnimationClipPreviewState(objectID).Active&&!status->text().isEmpty();});
    runtime->Update(.35f);
    auto playback=preview->GetAnimationClipPreviewState(objectID);
    Check(playback.Active&&playback.Playing&&playback.Clip==runClip&&playback.Time>.3f,
        "Direct Clip preview did not advance while Controller Speed remains zero");
    Check(bone->GetTransform()->GetWorldMatrix()(0,3)>17
        &&bone->GetTransform()->GetLocalPosition().X==4,"Direct Clip preview evaluated the Controller clip or modified source TRS");
    Check(animator->GetPlaybackState().CurrentState==controllerBefore.CurrentState,
        "Direct Clip preview changed the running Controller state");

    pause->click();playback=preview->GetAnimationClipPreviewState(objectID);
    Check(playback.Active&&!playback.Playing,"Direct Clip Pause did not freeze the preview");
    const float pausedTime=playback.Time;
    runtime->Update(.5f);
    Check(preview->GetAnimationClipPreviewState(objectID).Time==pausedTime,"Paused direct Clip advanced with Runtime updates");
    seconds->setValue(.5);seek->click();
    Check(std::fabs(preview->GetAnimationClipPreviewState(objectID).Time-.5f)<.001f
        &&!preview->GetAnimationClipPreviewState(objectID).Playing
        &&std::fabs(bone->GetTransform()->GetWorldMatrix()(0,3)-19)<.001f,
        "Direct Clip time entry did not seek the chosen clip while paused");
    Check(QMetaObject::invokeMethod(slider,"sliderPressed",Qt::DirectConnection),"Cannot begin the direct Clip timeline drag");
    slider->setValue(7500);
    Check(QMetaObject::invokeMethod(slider,"sliderReleased",Qt::DirectConnection),"Cannot finish the direct Clip timeline drag");
    Check(std::fabs(preview->GetAnimationClipPreviewState(objectID).Time-.75f)<.001f
        &&std::fabs(bone->GetTransform()->GetWorldMatrix()(0,3)-21.5f)<.001f,
        "Direct Clip timeline drag used the wrong duration or did not apply a pose");

    speed->setValue(2);loop->setChecked(false);
    playback=preview->GetAnimationClipPreviewState(objectID);
    Check(playback.Speed==2&&!playback.Loop&&!playback.Playing,
        "Direct Clip options reset paused playback or were not passed to Runtime");
    play->click();Check(preview->GetAnimationClipPreviewState(objectID).Playing,"Direct Clip Play did not resume the preview");
    Check(QMetaObject::invokeMethod(slider,"sliderPressed",Qt::DirectConnection),"Cannot scrub the playing direct Clip");
    Check(!preview->GetAnimationClipPreviewState(objectID).Playing,"Timeline drag did not pause a playing direct Clip");
    slider->setValue(9000);
    Check(QMetaObject::invokeMethod(slider,"sliderReleased",Qt::DirectConnection),"Cannot release the playing direct Clip timeline");
    Check(preview->GetAnimationClipPreviewState(objectID).Playing,"Timeline release did not restore prior playback");
    seconds->setValue(.9);seek->click();runtime->Update(1);
    playback=preview->GetAnimationClipPreviewState(objectID);
    Check(playback.Active&&!playback.Playing&&std::fabs(playback.Time-1)<.001f
        &&std::fabs(bone->GetTransform()->GetWorldMatrix()(0,3)-24)<.001f,
        "Non-looping direct Clip did not stop at the final pose");
    play->click();runtime->Update(.1f);
    playback=preview->GetAnimationClipPreviewState(objectID);
    Check(playback.Playing&&playback.Time>.1f&&playback.Time<.5f,
        "Play after a non-looping Clip finished did not restart it from the beginning");

    // Select a second Clip through the Module, then verify that the View targets it.
    // The in-memory provider deliberately has no source Meta; this avoids file import
    // work and exercises the same transient identity synchronization as a restored panel.
    Check(preview->PlayAnimationClip(objectID,defaultClip,.25f,1,true,error),error);
    pump([&]{return std::fabs(speed->value()-1)<.001&&loop->isChecked();});
    pause->click();seconds->setValue(.5);seek->click();
    Check(preview->GetAnimationClipPreviewState(objectID).Clip==defaultClip
        &&std::fabs(bone->GetTransform()->GetWorldMatrix()(0,3)-9)<.001f,
        "Direct Clip switch did not replace the sampled motion immediately");
    Check(animator->GetController()==controller&&animator->GetDefaultClip()==defaultClip
        &&animator->GetSpeed()==1&&animator->GetLoop(),"Transient preview replaced authored Animator settings");
    double parameter=1;
    Check(animator->GetParameter("Speed",parameter)&&parameter==0,"Direct Clip preview replaced Controller parameters");
    Check(notifications.Count==0,"Direct Clip preview dispatched gameplay or state-machine notifications");
    Check(!scenes->HasUnsavedChanges()&&scenes->SaveScene(error),error.empty()?"Direct Clip preview dirtied the scene":error);
    Check(ReadFile(root.filePath("Assets/Scenes/AnimatorInspector.scene"))==savedBefore,
        "Saving direct Clip preview persisted its selection, playback, or evaluated pose");
    stop->click();tick();
    Check(!preview->GetAnimationClipPreviewState(objectID).Active
        &&bone->GetTransform()->GetWorldMatrix()(0,3)==4&&!animator->GetPlaybackState().Playing,
        "Direct Clip Restore did not clear the evaluated pose and freeze the Controller");
    Check(preview->PlayAnimationClip(objectID,runClip,.4f,1,true,error),error);
    mode->setCurrentIndex(1);
    Check(!preview->GetAnimationClipPreviewState(objectID).Active
        &&bone->GetTransform()->GetWorldMatrix()(0,3)==4&&!direct->isVisible(),
        "Switching to Controller mode retained the direct Clip preview pose");
    std::cout<<"PASS Designer direct Clip preview: Controller bypass, pause/seek/timeline, options, Clip switching and source-only save\n";
}

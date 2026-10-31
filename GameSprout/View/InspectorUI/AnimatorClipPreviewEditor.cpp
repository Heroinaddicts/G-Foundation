#include "AnimatorClipPreviewEditor.h"
#include "ResourceField.h"
#include "RuntimeResourceApi.h"
#include "ui_AnimatorClipPreviewEditor.h"
#include <QSignalBlocker>
#include <QTimer>
#include <algorithm>
#include <cmath>

AnimatorClipPreviewEditor::AnimatorClipPreviewEditor(QWidget* parent)
    :QWidget(parent),_Ui(new Ui::AnimatorClipPreviewEditorForm()) {
    _Ui->setupUi(this);
    _Ui->clipErrorLabel->hide();
    _Ui->directClipResource->Changed=[this](const GameSproutRuntime::ResourceReference& clip){SelectClip(clip);};
    QObject::connect(_Ui->relatedClipCombo,qOverload<int>(&QComboBox::activated),this,[this](int index){
        if(index<0)return;
        SelectClip({_RelatedGuid,_Ui->relatedClipCombo->itemData(index).toULongLong()});
    });
    QObject::connect(_Ui->clipPlayButton,&QPushButton::clicked,this,[this]{
        if(!_Context.Scenes)return;
        const auto state=_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID);
        if(state.Active&&state.Clip==_SelectedClip&&(state.Loop||state.Time<state.Duration)) {
            std::string error;
            if(!_Context.Scenes->SetAnimationClipPreviewPlaying(_Context.ObjectID,true,error))ShowError(error);
            else ShowError({});
        } else Start();
        Refresh();
    });
    QObject::connect(_Ui->clipPauseButton,&QPushButton::clicked,this,[this]{
        if(!_Context.Scenes)return;
        const auto state=_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID);
        std::string error;
        if(state.Active&&!state.Playing&&!state.Loop&&state.Time>=state.Duration)Start();
        else if(state.Active&&!_Context.Scenes->SetAnimationClipPreviewPlaying(_Context.ObjectID,!state.Playing,error))ShowError(error);
        else ShowError({});
        Refresh();
    });
    QObject::connect(_Ui->clipStopButton,&QPushButton::clicked,this,[this]{StopPreview();});
    QObject::connect(_Ui->clipSeekButton,&QPushButton::clicked,this,[this]{SetTime(float(_Ui->clipSeekSpin->value()));});
    QObject::connect(_Ui->clipSpeedSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{ChangeOptions();});
    QObject::connect(_Ui->clipLoopCheck,&QCheckBox::toggled,this,[this]{ChangeOptions();});
    QObject::connect(_Ui->clipTimeSlider,&QSlider::sliderPressed,this,[this]{
        if(!_Context.Scenes)return;
        const auto state=_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID);
        _ResumeAfterScrub=state.Active&&state.Playing;
        if(_ResumeAfterScrub){std::string error;if(!_Context.Scenes->SetAnimationClipPreviewPlaying(_Context.ObjectID,false,error)){_ResumeAfterScrub=false;ShowError(error);}}
    });
    QObject::connect(_Ui->clipTimeSlider,&QSlider::valueChanged,this,[this](int value){
        if(_Loading||!_Context.Scenes)return;
        const auto state=_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID);
        if(state.Active)SetTime(state.Duration*float(value)/float(_Ui->clipTimeSlider->maximum()));
    });
    QObject::connect(_Ui->clipTimeSlider,&QSlider::sliderReleased,this,[this]{
        if(_ResumeAfterScrub&&_Context.Scenes){std::string error;if(!_Context.Scenes->SetAnimationClipPreviewPlaying(_Context.ObjectID,true,error))ShowError(error);}
        _ResumeAfterScrub=false;Refresh();
    });
    auto* timer=new QTimer(this);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,this,[this]{if(isVisible())Refresh();});timer->start();
}
AnimatorClipPreviewEditor::~AnimatorClipPreviewEditor(){StopPreview();delete _Ui;}
void AnimatorClipPreviewEditor::Bind(const ComponentEditContext& context) {
    const bool changed=context.Scenes!=_Context.Scenes||context.SceneID!=_Context.SceneID||context.ObjectID!=_Context.ObjectID;
    if(changed)StopPreview();
    _Context=context;
    _Ui->directClipResource->Bind(context.Assets,context.Library,context.Selection,eAssetType::AnimationClip);
    _Ui->directClipResource->SetContextKey(context.Key("Animator","PreviewClip"));
    if(changed) {
        _SelectedClip={};_RelatedGuid.clear();_RelatedRevision=0;_ResumeAfterScrub=false;_HadPlayback=false;
        if(auto* object=context.Resolve())if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())_SelectedClip=animator->GetDefaultClip();
        const QSignalBlocker speed(_Ui->clipSpeedSpin),loop(_Ui->clipLoopCheck);
        _Ui->clipSpeedSpin->setValue(1);_Ui->clipLoopCheck->setChecked(true);
        ShowError({});
    }
    _Ui->directClipResource->SetValue(_SelectedClip);Refresh();
}
void AnimatorClipPreviewEditor::ShowError(const std::string& error) {
    _Ui->clipErrorLabel->setText(QString::fromStdString(error));_Ui->clipErrorLabel->setVisible(!error.empty());
}
bool AnimatorClipPreviewEditor::Start(float seconds) {
    if(!_Context.Scenes)return false;
    if(_SelectedClip.Guid.empty()||!_SelectedClip.LocalID){ShowError("请先选择一个动画 Clip。");return false;}
    std::string error;
    const bool result=_Context.Scenes->PlayAnimationClip(_Context.ObjectID,_SelectedClip,seconds,float(_Ui->clipSpeedSpin->value()),_Ui->clipLoopCheck->isChecked(),error);
    ShowError(error);return result;
}
void AnimatorClipPreviewEditor::SelectClip(const GameSproutRuntime::ResourceReference& clip) {
    if(_Loading)return;
    _SelectedClip=clip;_Ui->directClipResource->SetValue(clip);
    if(clip.Guid.empty()){StopPreview();return;}
    Start();Refresh();
}
void AnimatorClipPreviewEditor::StopPreview() {
    _ResumeAfterScrub=false;_HadPlayback=false;
    if(_Context.Scenes)_Context.Scenes->StopAnimationClipPreview(_Context.ObjectID);
    if(_Ui){ShowError({});Refresh();}
}
void AnimatorClipPreviewEditor::SetTime(float seconds) {
    if(!_Context.Scenes)return;
    const auto state=_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID);
    std::string error;
    if(!state.Active) {
        if(!Start(seconds))return;
        if(!_Context.Scenes->SetAnimationClipPreviewPlaying(_Context.ObjectID,false,error))ShowError(error);
    } else if(!_Context.Scenes->SeekAnimationClipPreview(_Context.ObjectID,seconds,error))ShowError(error);
    else ShowError({});
    Refresh();
}
void AnimatorClipPreviewEditor::ChangeOptions() {
    if(_Loading||!_Context.Scenes)return;
    const auto state=_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID);
    if(!state.Active)return;
    if(Start(state.Time)&&!state.Playing){std::string error;if(!_Context.Scenes->SetAnimationClipPreviewPlaying(_Context.ObjectID,false,error))ShowError(error);}
    Refresh();
}
void AnimatorClipPreviewEditor::RefreshRelatedClips(std::uint64_t revision) {
    if(_RelatedGuid==_SelectedClip.Guid&&_RelatedRevision==revision&&_Ui->relatedClipCombo->count()) {
        const QSignalBlocker block(_Ui->relatedClipCombo);
        _Ui->relatedClipCombo->setCurrentIndex(_Ui->relatedClipCombo->findData(QVariant::fromValue<qulonglong>(_SelectedClip.LocalID)));return;
    }
    _RelatedGuid=_SelectedClip.Guid;_RelatedRevision=revision;
    const QSignalBlocker block(_Ui->relatedClipCombo);_Ui->relatedClipCombo->clear();
    if(!_Context.Library||_RelatedGuid.empty())return;
    AssetImportManifest manifest;
    if(!_Context.Library->GetManifest(Guid(_RelatedGuid),manifest))return;
    for(const auto& asset:manifest.Assets)if(asset.Type==eAssetType::AnimationClip)
        _Ui->relatedClipCombo->addItem(QString::fromStdString(asset.Name),QVariant::fromValue<qulonglong>(asset.LocalID));
    _Ui->relatedClipCombo->setCurrentIndex(_Ui->relatedClipCombo->findData(QVariant::fromValue<qulonglong>(_SelectedClip.LocalID)));
}
void AnimatorClipPreviewEditor::Refresh() {
    if(_Loading)return;
    _Loading=true;struct Guard{bool& Value;~Guard(){Value=false;}} guard{_Loading};
    const auto state=_Context.Scenes?_Context.Scenes->GetAnimationClipPreviewState(_Context.ObjectID):GameSproutRuntime::AnimationClipPlaybackState{};
    if(_HadPlayback&&!state.Active) {
        if(auto* object=_Context.Resolve())if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())
            if(!animator->GetLastError().empty())ShowError(animator->GetLastError());
    }
    _HadPlayback=state.Active;
    if(state.Active) {
        if(state.Clip!=_SelectedClip){_SelectedClip=state.Clip;_Ui->directClipResource->SetValue(_SelectedClip);}
        if(!_Ui->clipSpeedSpin->hasFocus()){const QSignalBlocker block(_Ui->clipSpeedSpin);_Ui->clipSpeedSpin->setValue(state.Speed);}
        const QSignalBlocker block(_Ui->clipLoopCheck);_Ui->clipLoopCheck->setChecked(state.Loop);
    }
    auto* runtime=_Context.Scenes?_Context.Scenes->GetRuntime():nullptr;
    auto* provider=runtime?runtime->GetResourceProvider():nullptr;
    RefreshRelatedClips(provider?provider->GetRevision():0);
    const bool available=_Context.Scenes&&_Context.Resolve();
    _Ui->clipPlayButton->setEnabled(available&&!_SelectedClip.Guid.empty());
    _Ui->clipPauseButton->setEnabled(available&&state.Active);
    _Ui->clipStopButton->setEnabled(available&&state.Active);
    _Ui->clipPauseButton->setText(state.Active&&!state.Playing?tr("继续"):tr("暂停"));
    _Ui->clipSeekButton->setEnabled(available&&!_SelectedClip.Guid.empty());
    _Ui->clipTimeSlider->setEnabled(state.Active&&state.Duration>0);
    if(!_Ui->clipTimeSlider->isSliderDown()) {
        const QSignalBlocker block(_Ui->clipTimeSlider);
        _Ui->clipTimeSlider->setValue(state.Duration>0?int(std::clamp(state.Time/state.Duration,0.f,1.f)*_Ui->clipTimeSlider->maximum()):0);
    }
    if(!_Ui->clipSeekSpin->hasFocus()){const QSignalBlocker block(_Ui->clipSeekSpin);_Ui->clipSeekSpin->setValue(state.Active?state.Time:0);}
    QString status=state.Active?tr("%1 · %2 / %3 s").arg(state.Playing?tr("播放中"):tr("已暂停")).arg(state.Time,0,'f',3).arg(state.Duration,0,'f',3):tr("选择动画后点击播放；切换 Clip 会立即播放。");
    _Ui->clipPlaybackLabel->setText(status);
}

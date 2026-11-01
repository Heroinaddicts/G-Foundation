#include "AnimatorClipPreviewEditor.h"
#include "ResourcePicker.h"
#include "../Shared/AssetDragPayload.h"
#include "RuntimeResourceApi.h"
#include "ui_AnimatorClipPreviewEditor.h"
#include <QSignalBlocker>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <algorithm>
#include <cmath>

AnimatorClipPreviewEditor::AnimatorClipPreviewEditor(QWidget* parent)
    :QWidget(parent),_Ui(new Ui::AnimatorClipPreviewEditorForm()) {
    _Ui->setupUi(this);
    _Ui->clipErrorLabel->hide();
    _Ui->relatedClipCombo->installEventFilter(this);
    _Ui->clipActionsButton->setMenu(_Ui->clipReferenceMenu);
    QObject::connect(_Ui->browseClipButton,&QToolButton::clicked,this,[this]{OpenPicker();});
    QObject::connect(_Ui->clearClipAction,&QAction::triggered,this,[this]{SelectClip({});});
    QObject::connect(_Ui->locateClipAction,&QAction::triggered,this,[this]{
        AssetReferenceCandidate candidate;std::string error;
        if(!_Context.Selection||!AssetReferenceResolver::Resolve(_Context.Assets,_Context.Library,_SelectedClip,eAssetType::AnimationClip,candidate,error,false)){ShowError(error);return;}
        _Context.Selection->SetAssetSelection({{candidate.Reference.Guid,candidate.IsMain?0:candidate.Reference.LocalID,!candidate.IsMain}});
    });
    QObject::connect(_Ui->relatedClipCombo,qOverload<int>(&QComboBox::activated),this,[this](int index){
        if(index<0)return;
        std::string error;
        if(!Choose({_RelatedGuid,_Ui->relatedClipCombo->itemData(index).toULongLong()},error))ShowError(error);
    });
    QObject::connect(_Ui->clipPlayButton,&QPushButton::clicked,this,[this]{
        if(!_Context.Preview)return;
        const auto state=_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID);
        if(state.Active&&state.Clip==_SelectedClip&&(state.Loop||state.Time<state.Duration)) {
            std::string error;
            if(!_Context.Preview->SetAnimationClipPreviewPlaying(_Context.ObjectID,true,error))ShowError(error);
            else ShowError({});
        } else Start();
        Refresh();
    });
    QObject::connect(_Ui->clipPauseButton,&QPushButton::clicked,this,[this]{
        if(!_Context.Preview)return;
        const auto state=_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID);
        std::string error;
        if(state.Active&&!state.Playing&&!state.Loop&&state.Time>=state.Duration)Start();
        else if(state.Active&&!_Context.Preview->SetAnimationClipPreviewPlaying(_Context.ObjectID,!state.Playing,error))ShowError(error);
        else ShowError({});
        Refresh();
    });
    QObject::connect(_Ui->clipStopButton,&QPushButton::clicked,this,[this]{StopPreview();});
    QObject::connect(_Ui->clipSeekButton,&QPushButton::clicked,this,[this]{SetTime(float(_Ui->clipSeekSpin->value()));});
    QObject::connect(_Ui->clipSeekSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double seconds){if(!_Loading)SetTime(float(seconds));});
    QObject::connect(_Ui->clipSpeedSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{ChangeOptions();});
    QObject::connect(_Ui->clipLoopCheck,&QCheckBox::toggled,this,[this]{ChangeOptions();});
    QObject::connect(_Ui->clipTimeSlider,&QSlider::sliderPressed,this,[this]{
        if(!_Context.Preview)return;
        const auto state=_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID);
        _ResumeAfterScrub=state.Active&&state.Playing;
        if(_ResumeAfterScrub){std::string error;if(!_Context.Preview->SetAnimationClipPreviewPlaying(_Context.ObjectID,false,error)){_ResumeAfterScrub=false;ShowError(error);}}
    });
    QObject::connect(_Ui->clipTimeSlider,&QSlider::valueChanged,this,[this](int value){
        if(_Loading||!_Context.Preview)return;
        const auto state=_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID);
        if(state.Active)SetTime(state.Duration*float(value)/float(_Ui->clipTimeSlider->maximum()));
    });
    QObject::connect(_Ui->clipTimeSlider,&QSlider::sliderReleased,this,[this]{
        if(_ResumeAfterScrub&&_Context.Preview){std::string error;if(!_Context.Preview->SetAnimationClipPreviewPlaying(_Context.ObjectID,true,error))ShowError(error);}
        _ResumeAfterScrub=false;Refresh();
    });
    auto* timer=new QTimer(this);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,this,[this]{if(isVisible())Refresh();});timer->start();
}
AnimatorClipPreviewEditor::~AnimatorClipPreviewEditor(){_Ui->relatedClipCombo->removeEventFilter(this);ClosePicker();StopPreview();delete _Ui;}
void AnimatorClipPreviewEditor::Bind(const ComponentEditContext& context) {
    const bool changed=context.Scenes!=_Context.Scenes||context.Runtime!=_Context.Runtime||context.Preview!=_Context.Preview||context.SceneID!=_Context.SceneID||context.ObjectID!=_Context.ObjectID;
    if(changed){ClosePicker();StopPreview();}
    _Context=context;
    if(changed) {
        _SelectedClip={};_RelatedGuid.clear();_RelatedRevision=0;_ResumeAfterScrub=false;_HadPlayback=false;
        if(auto* object=context.Resolve())if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())_SelectedClip=animator->GetDefaultClip();
        const QSignalBlocker speed(_Ui->clipSpeedSpin),loop(_Ui->clipLoopCheck);
        _Ui->clipSpeedSpin->setValue(1);_Ui->clipLoopCheck->setChecked(true);
        ShowError({});
    }
    Refresh();
}
void AnimatorClipPreviewEditor::ShowError(const std::string& error) {
    _Ui->clipErrorLabel->setText(QString::fromStdString(error));_Ui->clipErrorLabel->setVisible(!error.empty());
}
bool AnimatorClipPreviewEditor::Start(float seconds) {
    if(!_Context.Preview)return false;
    if(_SelectedClip.Guid.empty()||!_SelectedClip.LocalID){ShowError("请先选择一个动画 Clip。");return false;}
    std::string error;
    const bool result=_Context.Preview->PlayAnimationClip(_Context.ObjectID,_SelectedClip,seconds,float(_Ui->clipSpeedSpin->value()),_Ui->clipLoopCheck->isChecked(),error);
    ShowError(error);return result;
}
void AnimatorClipPreviewEditor::SelectClip(const GameSproutRuntime::ResourceReference& clip) {
    if(_Loading)return;
    _SelectedClip=clip;
    if(clip.Guid.empty()){StopPreview();return;}
    Start();Refresh();
}
void AnimatorClipPreviewEditor::StopPreview() {
    _ResumeAfterScrub=false;_HadPlayback=false;
    if(_Context.Preview)_Context.Preview->StopAnimationClipPreview(_Context.ObjectID);
    if(_Ui){ShowError({});Refresh();}
}
void AnimatorClipPreviewEditor::SetTime(float seconds) {
    if(!_Context.Preview)return;
    const auto state=_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID);
    std::string error;
    if(!state.Active) {
        if(!Start(seconds))return;
        if(!_Context.Preview->SetAnimationClipPreviewPlaying(_Context.ObjectID,false,error))ShowError(error);
    } else if(!_Context.Preview->SeekAnimationClipPreview(_Context.ObjectID,seconds,error))ShowError(error);
    else ShowError({});
    Refresh();
}
void AnimatorClipPreviewEditor::ChangeOptions() {
    if(_Loading||!_Context.Preview)return;
    const auto state=_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID);
    if(!state.Active)return;
    if(Start(state.Time)&&!state.Playing){std::string error;if(!_Context.Preview->SetAnimationClipPreviewPlaying(_Context.ObjectID,false,error))ShowError(error);}
    Refresh();
}
void AnimatorClipPreviewEditor::RefreshRelatedClips(std::uint64_t revision) {
    const int current=_Ui->relatedClipCombo->findData(QVariant::fromValue<qulonglong>(_SelectedClip.LocalID));
    if(_RelatedGuid==_SelectedClip.Guid&&_RelatedRevision==revision&&current>=0) {
        const QSignalBlocker block(_Ui->relatedClipCombo);
        _Ui->relatedClipCombo->setCurrentIndex(current);
        _Ui->relatedClipCombo->setToolTip(_Ui->relatedClipCombo->currentText()+(_RelatedSourcePath.empty()?QString{}:QStringLiteral("\nAssets/")+QString::fromStdString(_RelatedSourcePath)));return;
    }
    _RelatedGuid=_SelectedClip.Guid;_RelatedRevision=revision;
    const QSignalBlocker block(_Ui->relatedClipCombo);_Ui->relatedClipCombo->clear();
    if(_RelatedGuid.empty())return;
    AssetImportManifest manifest;
    if(_Context.Library&&_Context.Library->GetManifest(Guid(_RelatedGuid),manifest))
        for(const auto& asset:manifest.Assets)if(asset.Type==eAssetType::AnimationClip)
            _Ui->relatedClipCombo->addItem(QString::fromStdString(asset.Name),QVariant::fromValue<qulonglong>(asset.LocalID));
    if(_SelectedClip.LocalID&&_Ui->relatedClipCombo->findData(QVariant::fromValue<qulonglong>(_SelectedClip.LocalID))<0)
        _Ui->relatedClipCombo->addItem(tr("当前动画（资源未登记）"),QVariant::fromValue<qulonglong>(_SelectedClip.LocalID));
    _Ui->relatedClipCombo->setCurrentIndex(_Ui->relatedClipCombo->findData(QVariant::fromValue<qulonglong>(_SelectedClip.LocalID)));
    _RelatedSourcePath=manifest.SourcePath;
    const QString source=QString::fromStdString(_RelatedSourcePath);
    _Ui->relatedClipCombo->setToolTip(_Ui->relatedClipCombo->currentText()+(source.isEmpty()?QString{}:QStringLiteral("\nAssets/")+source));
}
void AnimatorClipPreviewEditor::Refresh() {
    if(_Loading)return;
    _Loading=true;struct Guard{bool& Value;~Guard(){Value=false;}} guard{_Loading};
    const auto state=_Context.Preview?_Context.Preview->GetAnimationClipPreviewState(_Context.ObjectID):GameSproutRuntime::AnimationClipPlaybackState{};
    if(_HadPlayback&&!state.Active) {
        if(auto* object=_Context.Resolve())if(auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>())
            if(!animator->GetLastError().empty())ShowError(animator->GetLastError());
    }
    _HadPlayback=state.Active;
    if(state.Active) {
        if(state.Clip!=_SelectedClip)_SelectedClip=state.Clip;
        if(!_Ui->clipSpeedSpin->hasFocus()){const QSignalBlocker block(_Ui->clipSpeedSpin);_Ui->clipSpeedSpin->setValue(state.Speed);}
        const QSignalBlocker block(_Ui->clipLoopCheck);_Ui->clipLoopCheck->setChecked(state.Loop);
    }
    auto* runtime=_Context.Runtime;
    auto* provider=runtime?runtime->GetResourceProvider():nullptr;
    RefreshRelatedClips(provider?provider->GetRevision():0);
    const bool available=_Context.Preview&&_Context.Resolve();
    _Ui->clipPlayButton->setEnabled(available&&!_SelectedClip.Guid.empty());
    _Ui->clipPlayButton->setVisible(!state.Playing);
    _Ui->clipPauseButton->setVisible(state.Playing);
    _Ui->clipPauseButton->setEnabled(available&&state.Active);
    _Ui->clipStopButton->setEnabled(available&&state.Active);
    _Ui->browseClipButton->setEnabled(available&&_Context.Assets&&_Context.Library);
    _Ui->clearClipAction->setEnabled(!_SelectedClip.Guid.empty());
    _Ui->locateClipAction->setEnabled(!_SelectedClip.Guid.empty()&&_Context.Selection);
    _Ui->clipSeekButton->setEnabled(available&&!_SelectedClip.Guid.empty());
    _Ui->clipTimeSlider->setEnabled(state.Active&&state.Duration>0);
    if(!_Ui->clipTimeSlider->isSliderDown()) {
        const QSignalBlocker block(_Ui->clipTimeSlider);
        _Ui->clipTimeSlider->setValue(state.Duration>0?int(std::clamp(state.Time/state.Duration,0.f,1.f)*_Ui->clipTimeSlider->maximum()):0);
    }
    if(!_Ui->clipSeekSpin->hasFocus()){const QSignalBlocker block(_Ui->clipSeekSpin);_Ui->clipSeekSpin->setValue(state.Active?state.Time:0);}
    QString status=state.Active?tr("%1 · %2 / %3 s").arg(state.Playing?tr("播放中"):tr("已暂停")).arg(state.Time,0,'f',2).arg(state.Duration,0,'f',2):QString{};
    _Ui->clipPlaybackLabel->setText(status);
    _Ui->clipPlaybackLabel->setVisible(state.Active);
}

void AnimatorClipPreviewEditor::ClosePicker() {
    ++_PickerGeneration;auto* picker=_Picker;_Picker=nullptr;
    if(picker){picker->Selected={};picker->close();}
}
bool AnimatorClipPreviewEditor::eventFilter(QObject* object,QEvent* event) {
    if(object==_Ui->relatedClipCombo&&event->type()==QEvent::KeyPress&&isEnabled()) {
        auto* key=static_cast<QKeyEvent*>(event);
        if((key->key()==Qt::Key_Delete||key->key()==Qt::Key_Backspace)&&key->modifiers()==Qt::NoModifier){SelectClip({});return true;}
    }
    return QWidget::eventFilter(object,event);
}
void AnimatorClipPreviewEditor::OpenPicker() {
    ClosePicker();
    auto* picker=new ResourcePicker(this);picker->setAttribute(Qt::WA_DeleteOnClose);_Picker=picker;
    const auto generation=_PickerGeneration;
    picker->SetCandidates(AssetReferenceResolver::Enumerate(_Context.Assets,_Context.Library,eAssetType::AnimationClip),eAssetType::AnimationClip);
    QObject::connect(picker,&QObject::destroyed,this,[this,picker]{if(_Picker==picker)_Picker=nullptr;});
    picker->Selected=[this,picker,generation](const GameSproutRuntime::ResourceReference& clip){
        if(_Picker!=picker||generation!=_PickerGeneration)return;
        std::string error;if(!Choose(clip,error)){picker->SetError(error);return;}ClosePicker();
    };
    picker->show();picker->raise();picker->activateWindow();
}
bool AnimatorClipPreviewEditor::Choose(const GameSproutRuntime::ResourceReference& clip,std::string& error) {
    AssetReferenceCandidate candidate;
    if(!AssetReferenceResolver::Resolve(_Context.Assets,_Context.Library,clip,eAssetType::AnimationClip,candidate,error))return false;
    SelectClip(candidate.Reference);return true;
}
bool AnimatorClipPreviewEditor::CanDrop(const QMimeData* mime,std::string& error) const {
    std::vector<ProjectAssetSelection> selected;std::vector<AssetReferenceCandidate> candidates;
    return AssetDragPayload::Decode(mime,selected)&&selected.size()==1
        &&AssetReferenceResolver::Candidates(_Context.Assets,_Context.Library,selected.front(),eAssetType::AnimationClip,candidates,error);
}
void AnimatorClipPreviewEditor::dragEnterEvent(QDragEnterEvent* event) {
    std::string error;if(CanDrop(event->mimeData(),error)){event->setDropAction(Qt::CopyAction);event->accept();}else event->ignore();
}
void AnimatorClipPreviewEditor::dragMoveEvent(QDragMoveEvent* event) {
    std::string error;if(CanDrop(event->mimeData(),error)){event->setDropAction(Qt::CopyAction);event->accept();}else event->ignore();
}
void AnimatorClipPreviewEditor::dropEvent(QDropEvent* event) {
    std::vector<ProjectAssetSelection> selected;std::vector<AssetReferenceCandidate> candidates;std::string error;
    if(!AssetDragPayload::Decode(event->mimeData(),selected)||selected.size()!=1
        ||!AssetReferenceResolver::Candidates(_Context.Assets,_Context.Library,selected.front(),eAssetType::AnimationClip,candidates,error)){
        ShowError(error.empty()?"请拖入一个动画 Clip 或含动画的模型。":error);event->ignore();return;
    }
    if(candidates.size()==1){if(!Choose(candidates.front().Reference,error)){ShowError(error);event->ignore();return;}}
    else {
        OpenPicker();if(_Picker)_Picker->SetCandidates(candidates,eAssetType::AnimationClip);
    }
    event->setDropAction(Qt::CopyAction);event->accept();
}

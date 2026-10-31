#include "AnimatorComponentEditor.h"
#include "ResourceField.h"
#include "AnimatorIKEditor.h"
#include "RuntimeResourceApi.h"
#include "ui_AnimatorComponentEditor.h"
#include <QTimer>
#include <QSignalBlocker>
#include <QHeaderView>
#include <cmath>
#include <algorithm>

namespace {
struct Settings {
    GameSproutRuntime::ResourceReference Skeleton,Controller,Clip,Avatar;
    float Speed=1;bool Auto=true,Loop=true;
    GameSproutRuntime::RootMotionMode Motion=GameSproutRuntime::RootMotionMode::Disabled;
};
Settings Read(const GameSproutRuntime::IAnimatorComponent& value) {
    return {value.GetSkeleton(),value.GetController(),value.GetDefaultClip(),value.GetAvatar(),value.GetSpeed(),value.GetPlayOnStart(),value.GetLoop(),value.GetRootMotionMode()};
}
}
AnimatorComponentEditor::AnimatorComponentEditor(QWidget* parent):ComponentEditor(parent),_Ui(new Ui::AnimatorComponentEditorForm()) {
    _Ui->setupUi(this);
    _Ui->ikEditor->Resolve=[this]{return Animator();};
    _Ui->animationNotificationsTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    _Ui->animationNotificationsTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    _Ui->animationNotificationsTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    _Ui->animationNotificationsTable->hide();_Ui->clearAnimationNotificationsButton->hide();
    QObject::connect(_Ui->animationNotificationsGroup,&QGroupBox::toggled,this,[this](bool open){_Ui->animationNotificationsTable->setVisible(open);_Ui->clearAnimationNotificationsButton->setVisible(open);});
    QObject::connect(_Ui->clearAnimationNotificationsButton,&QPushButton::clicked,this,[this]{_Notifications.clear();_NotificationsChanged=true;RefreshNotifications();});
    QObject::connect(_Ui->rootMotionModeCombo,qOverload<int>(&QComboBox::activated),this,[this]{CommitSettings();});
    const auto changed=[this](const GameSproutRuntime::ResourceReference&){CommitSettings();};
    _Ui->skeletonResource->Changed=changed;_Ui->controllerResource->Changed=changed;_Ui->clipResource->Changed=changed;_Ui->avatarResource->Changed=changed;
    QObject::connect(_Ui->speedSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{CommitSettings();});
    QObject::connect(_Ui->playOnStartCheck,&QCheckBox::clicked,this,[this]{CommitSettings();});
    QObject::connect(_Ui->loopCheck,&QCheckBox::clicked,this,[this]{CommitSettings();});
    QObject::connect(_Ui->previewLayerCombo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{RefreshLayerStates();RefreshPlayback();});
    QObject::connect(_Ui->runtimeWeightSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double weight){
        if(_Loading)return;auto* animator=Animator();const int layer=_Ui->previewLayerCombo->currentIndex();
        if(animator&&layer>0&&!animator->SetLayerWeight(std::uint32_t(layer),float(weight)))Fail(animator->GetLastError());
    });
    QObject::connect(_Ui->playButton,&QPushButton::clicked,this,[this]{
        if(auto* animator=Animator()) {
            if(!animator->Play(_Ui->stateCombo->currentText().toStdString(),0,std::uint32_t(std::max(0,_Ui->previewLayerCombo->currentIndex())))) {Fail(animator->GetLastError());return;}
            if(_Context.Scenes)_Context.Scenes->SetAnimationPreviewPlaying(true);
        }
    });
    QObject::connect(_Ui->pauseButton,&QPushButton::clicked,this,[this]{if(auto* animator=Animator())animator->Pause();RefreshPlayback();});
    QObject::connect(_Ui->stopButton,&QPushButton::clicked,this,[this]{if(auto* animator=Animator())animator->Stop();RefreshPlayback();});
    QObject::connect(_Ui->seekButton,&QPushButton::clicked,this,[this]{if(auto* animator=Animator())if(!animator->Seek(float(_Ui->seekSpin->value()),std::uint32_t(std::max(0,_Ui->previewLayerCombo->currentIndex()))))Fail(animator->GetLastError());RefreshPlayback();});
    QObject::connect(_Ui->crossFadeButton,&QPushButton::clicked,this,[this]{
        if(auto* animator=Animator()) {
            if(!animator->CrossFade(_Ui->stateCombo->currentText().toStdString(),0.2f,0,std::uint32_t(std::max(0,_Ui->previewLayerCombo->currentIndex()))))Fail(animator->GetLastError());
            else if(_Context.Scenes)_Context.Scenes->SetAnimationPreviewPlaying(true);
        }
    });
    QObject::connect(_Ui->setParameterButton,&QPushButton::clicked,this,[this]{
        auto* animator=Animator();if(!animator||_Ui->parameterCombo->currentIndex()<0)return;
        const auto name=_Ui->parameterCombo->currentText().toStdString();const double value=_Ui->parameterSpin->value();
        const auto type=static_cast<GameSproutRuntime::AnimatorParameterType>(_Ui->parameterCombo->currentData().toInt());
        bool accepted=false;
        switch(type) {
        case GameSproutRuntime::AnimatorParameterType::Float:accepted=animator->SetFloat(name,float(value));break;
        case GameSproutRuntime::AnimatorParameterType::Int:accepted=animator->SetInt(name,static_cast<std::int32_t>(value));break;
        case GameSproutRuntime::AnimatorParameterType::Bool:accepted=animator->SetBool(name,value!=0);break;
        case GameSproutRuntime::AnimatorParameterType::Trigger:accepted=animator->SetTrigger(name);break;
        }
        if(!accepted)Fail(animator->GetLastError());RefreshPlayback();
    });
    auto* timer=new QTimer(this);timer->setInterval(100);
    QObject::connect(timer,&QTimer::timeout,this,[this]{if(isVisible())RefreshPlayback();});timer->start();
}
AnimatorComponentEditor::~AnimatorComponentEditor(){delete _Ui;}
GameSproutRuntime::IAnimatorComponent* AnimatorComponentEditor::Animator() const {
    auto* object=_Context.Resolve();return object?object->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
}
void AnimatorComponentEditor::ContextChanged() {
    _Notifications.clear();_NotificationsChanged=true;
    _Ui->ikEditor->ResetContext();
    _Ui->avatarResource->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Avatar);
    _Ui->avatarResource->SetContextKey(_Context.Key("Animator","Avatar"));
    _Ui->skeletonResource->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::Skeleton);
    _Ui->controllerResource->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::AnimatorController);
    _Ui->clipResource->Bind(_Context.Assets,_Context.Library,_Context.Selection,eAssetType::AnimationClip);
    _Ui->skeletonResource->SetContextKey(_Context.Key("Animator","Skeleton"));
    _Ui->controllerResource->SetContextKey(_Context.Key("Animator","Controller"));
    _Ui->clipResource->SetContextKey(_Context.Key("Animator","Clip"));_HasController=false;
}
void AnimatorComponentEditor::ReadValues(GameSproutRuntime::IComponent& component) {
    auto& animator=static_cast<GameSproutRuntime::IAnimatorComponent&>(component);
    _Ui->skeletonResource->SetValue(animator.GetSkeleton());_Ui->controllerResource->SetValue(animator.GetController());
    _Ui->avatarResource->SetValue(animator.GetAvatar());_Ui->clipResource->SetValue(animator.GetDefaultClip());_Ui->speedSpin->setValue(animator.GetSpeed());
    _Ui->rootMotionModeCombo->setCurrentIndex(int(animator.GetRootMotionMode()));
    _Ui->playOnStartCheck->setChecked(animator.GetPlayOnStart());_Ui->loopCheck->setChecked(animator.GetLoop());
    RefreshPlayback();
}
void AnimatorComponentEditor::CommitSettings() {
    if(_Loading)return;
    auto* animator=Animator();if(!animator)return;
    const auto previous=Read(*animator);
    const Settings next{_Ui->skeletonResource->GetValue(),_Ui->controllerResource->GetValue(),_Ui->clipResource->GetValue(),_Ui->avatarResource->GetValue(),float(_Ui->speedSpin->value()),_Ui->playOnStartCheck->isChecked(),_Ui->loopCheck->isChecked(),static_cast<GameSproutRuntime::RootMotionMode>(_Ui->rootMotionModeCombo->currentIndex())};
    const auto operation=[](Settings value){return [value](GameSproutRuntime::IGameObject& object,std::string& error){
        auto* component=object.GetComponent<GameSproutRuntime::IAnimatorComponent>();
        if(!component){error="Animator 已被移除。";return false;}
        if(!component->SetSpeed(value.Speed)){error="无效播放倍率。";return false;}
        component->SetSkeleton(value.Skeleton);component->SetController(value.Controller);component->SetDefaultClip(value.Clip);component->SetAvatar(value.Avatar);
        if(!component->SetRootMotionMode(value.Motion)){error="无效根运动模式。";return false;}
        component->SetPlayOnStart(value.Auto);component->SetLoop(value.Loop);return true;
    };};
    _Context.Execute("修改 Animator",operation(next),operation(previous));Reload();
}
void AnimatorComponentEditor::RefreshPlayback() {
    RefreshNotifications();
    auto* animator=Animator();if(!animator)return;
    _Ui->ikEditor->SetAvailable(!animator->GetAvatar().Guid.empty());
    const auto controller=animator->GetController();
    auto* runtime=_Context.Scenes?_Context.Scenes->GetRuntime():nullptr;
    auto* provider=runtime?runtime->GetResourceProvider():nullptr;
    const auto revision=provider?provider->GetRevision():0;
    if(!_HasController||controller!=_LoadedController||revision!=_ControllerRevision) {
        _ControllerRevision=revision;
        _HasController=true;_LoadedController=controller;
        const QSignalBlocker layerBlock(_Ui->previewLayerCombo);const int selectedLayer=_Ui->previewLayerCombo->currentIndex();
        _Ui->stateCombo->clear();_Ui->parameterCombo->clear();_Ui->previewLayerCombo->clear();_ControllerData={};std::string error;
        if(provider&&!controller.Guid.empty()&&provider->LoadAnimatorController(controller,_ControllerData,error)) {
            for(const auto& layer:_ControllerData.Layers)_Ui->previewLayerCombo->addItem(QString::fromStdString(layer.Name));
            for(const auto& parameter:_ControllerData.Parameters)_Ui->parameterCombo->addItem(QString::fromStdString(parameter.Name),int(parameter.Type));
        }else _Ui->previewLayerCombo->addItem(tr("默认动画"));
        _Ui->previewLayerCombo->setCurrentIndex(std::clamp(selectedLayer,0,_Ui->previewLayerCombo->count()-1));RefreshLayerStates();
    }
    const bool hasMotion=animator->GetRootMotionMode()!=GameSproutRuntime::RootMotionMode::Disabled;
    _Ui->rootMotionDeltaLabel->setVisible(hasMotion);_Ui->rootMotionRotationLabel->setVisible(hasMotion);
    const auto delta=animator->GetRootMotionDelta();
    _Ui->rootMotionDeltaLabel->setText(tr("动画请求位移：%1, %2, %3 m").arg(delta.Translation.X,0,'g',5).arg(delta.Translation.Y,0,'g',5).arg(delta.Translation.Z,0,'g',5));
    _Ui->rootMotionRotationLabel->setText(tr("动画请求旋转 XYZW：%1, %2, %3, %4").arg(delta.Rotation.X,0,'g',5).arg(delta.Rotation.Y,0,'g',5).arg(delta.Rotation.Z,0,'g',5).arg(delta.Rotation.W,0,'g',5));
    const auto layer=std::uint32_t(std::max(0,_Ui->previewLayerCombo->currentIndex()));
    if(!_Ui->runtimeWeightSpin->hasFocus()){const QSignalBlocker weightBlock(_Ui->runtimeWeightSpin);_Ui->runtimeWeightSpin->setValue(animator->GetLayerWeight(layer));}
    _Ui->runtimeWeightSpin->setEnabled(layer>0);
    const auto state=animator->GetPlaybackState(layer);
    QString text=_Ui->previewLayerCombo->currentText()+QStringLiteral(" · ")+QString::fromStdString(state.CurrentState)+QStringLiteral(" · %1 s").arg(state.Time,0,'f',3);
    if(!state.NextState.empty())text+=QStringLiteral(" → %1 · %2%").arg(QString::fromStdString(state.NextState)).arg(int(state.TransitionWeight*100));
    if(!animator->GetLastError().empty())text=QString::fromStdString(animator->GetLastError());
    _Ui->playbackLabel->setText(text);
}

void AnimatorComponentEditor::OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) {
    if(event.SceneID!=_Context.SceneID||event.ObjectID!=_Context.ObjectID)return;
    if(_Notifications.size()==64)_Notifications.pop_front();
    _Notifications.push_back(event);_NotificationsChanged=true;
}
void AnimatorComponentEditor::RefreshNotifications() {
    if(!_NotificationsChanged)return;
    _NotificationsChanged=false;_Ui->animationNotificationsTable->setRowCount(0);
    for(const auto& event:_Notifications) {
        const int row=_Ui->animationNotificationsTable->rowCount();_Ui->animationNotificationsTable->insertRow(row);
        const QString first=event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent?QString::number(event.Time,'f',3)+" s":QString::fromStdString(event.State);
        const QString name=event.Type==GameSproutRuntime::AnimationNotificationType::StateEnter?tr("进入状态"):
            event.Type==GameSproutRuntime::AnimationNotificationType::StateExit?tr("退出状态"):QString::fromStdString(event.Name);
        _Ui->animationNotificationsTable->setItem(row,0,new QTableWidgetItem(QString::fromStdString(event.LayerName)+QStringLiteral(" · ")+first));
        _Ui->animationNotificationsTable->setItem(row,1,new QTableWidgetItem(name));
        _Ui->animationNotificationsTable->setItem(row,2,new QTableWidgetItem(QString::fromStdString(event.Payload)));
    }
    _Ui->animationNotificationsTable->scrollToBottom();
}

void AnimatorComponentEditor::RefreshLayerStates(){
    _Ui->stateCombo->clear();const int selected=_Ui->previewLayerCombo->currentIndex();
    int graph=selected;for(std::size_t count=0;graph>=0&&graph<int(_ControllerData.Layers.size())&&_ControllerData.Layers[graph].SyncedLayerIndex>=0&&count<_ControllerData.Layers.size();++count)graph=_ControllerData.Layers[graph].SyncedLayerIndex;
    if(graph>=0&&graph<int(_ControllerData.Layers.size()))for(const auto& state:_ControllerData.Layers[graph].States)
        _Ui->stateCombo->addItem(QString::fromStdString(state.Name));
}

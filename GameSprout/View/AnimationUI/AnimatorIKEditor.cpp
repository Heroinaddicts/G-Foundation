#include "AnimatorIKEditor.h"
#include "ui_AnimatorIKEditor.h"
#include <cmath>
namespace {
bool Number(QTableWidget* table,int row,int column,float& output){bool valid=false;const double value=table->item(row,column)->text().toDouble(&valid);
    if(!valid||!std::isfinite(value)||std::fabs(value)>1e20)return false;output=float(value);return true;
}
}
AnimatorIKEditor::AnimatorIKEditor(QWidget* parent):QWidget(parent),_Ui(new Ui::AnimatorIKEditorForm){
    _Ui->setupUi(this);_Ui->controls->hide();
    connect(_Ui->ikGroup,&QGroupBox::toggled,_Ui->controls,&QWidget::setVisible);
    connect(_Ui->goalCombo,&QComboBox::currentIndexChanged,this,[this]{LoadGoal();});
    connect(_Ui->applyGoalButton,&QPushButton::clicked,this,[this]{ApplyGoal();});
    connect(_Ui->applyLookAtButton,&QPushButton::clicked,this,[this]{ApplyLookAt();});
    connect(_Ui->clearIKButton,&QPushButton::clicked,this,[this]{if(auto* value=Resolve?Resolve():nullptr)value->ClearIKGoals();ResetContext();});
}
AnimatorIKEditor::~AnimatorIKEditor(){delete _Ui;}
void AnimatorIKEditor::ResetContext(){LoadGoal();auto* animator=Resolve?Resolve():nullptr;const auto value=animator?animator->GetLookAt():GameSproutRuntime::AnimatorLookAt{};
    const float position[]={value.Position.X,value.Position.Y,value.Position.Z};for(int index=0;index<3;++index)_Ui->lookAtTable->item(0,index)->setText(QString::number(position[index],'g',8));
    _Ui->lookWeightSpin->setValue(value.Weight);_Ui->bodyWeightSpin->setValue(value.BodyWeight);_Ui->headWeightSpin->setValue(value.HeadWeight);_Ui->eyesWeightSpin->setValue(value.EyesWeight);_Ui->clampWeightSpin->setValue(value.ClampWeight);_Ui->statusLabel->clear();
}
void AnimatorIKEditor::LoadGoal(){auto* animator=Resolve?Resolve():nullptr;const auto value=animator?animator->GetIKGoal(static_cast<GameSproutRuntime::AvatarIKGoal>(_Ui->goalCombo->currentIndex())):GameSproutRuntime::AnimatorIKGoal{};
    const float rows[3][4]={{value.Position.X,value.Position.Y,value.Position.Z,0},{value.Rotation.X,value.Rotation.Y,value.Rotation.Z,value.Rotation.W},{value.Hint.X,value.Hint.Y,value.Hint.Z,0}};
    for(int row=0;row<3;++row)for(int column=0;column<4;++column){auto* item=_Ui->goalTable->item(row,column);item->setText(QString::number(rows[row][column],'g',8));if(row!=1&&column==3)item->setFlags(item->flags()&~Qt::ItemIsEditable);}
    _Ui->positionWeightSpin->setValue(value.PositionWeight);_Ui->rotationWeightSpin->setValue(value.RotationWeight);_Ui->hintWeightSpin->setValue(value.HintWeight);
}
void AnimatorIKEditor::ApplyGoal(){auto* animator=Resolve?Resolve():nullptr;if(!animator){_Ui->statusLabel->setText(tr("Animator 已不可用。"));return;}
    GameSproutRuntime::AnimatorIKGoal value;float rows[3][4]{};
    for(int row=0;row<3;++row)for(int column=0;column<(row==1?4:3);++column)if(!Number(_Ui->goalTable,row,column,rows[row][column])){_Ui->statusLabel->setText(tr("IK 目标必须是有限数值。"));return;}
    value.Position={rows[0][0],rows[0][1],rows[0][2]};value.Rotation={rows[1][0],rows[1][1],rows[1][2],rows[1][3]};value.Hint={rows[2][0],rows[2][1],rows[2][2]};
    value.PositionWeight=float(_Ui->positionWeightSpin->value());value.RotationWeight=float(_Ui->rotationWeightSpin->value());value.HintWeight=float(_Ui->hintWeightSpin->value());
    _Ui->statusLabel->setText(animator->SetIKGoal(static_cast<GameSproutRuntime::AvatarIKGoal>(_Ui->goalCombo->currentIndex()),value)?tr("临时目标已设置；由启用 IK Pass 的层求解。"):QString::fromStdString(animator->GetLastError()));
}
void AnimatorIKEditor::ApplyLookAt(){auto* animator=Resolve?Resolve():nullptr;if(!animator)return;GameSproutRuntime::AnimatorLookAt value;
    if(!Number(_Ui->lookAtTable,0,0,value.Position.X)||!Number(_Ui->lookAtTable,0,1,value.Position.Y)||!Number(_Ui->lookAtTable,0,2,value.Position.Z)){_Ui->statusLabel->setText(tr("Look At 目标必须是有限数值。"));return;}
    value.Weight=float(_Ui->lookWeightSpin->value());value.BodyWeight=float(_Ui->bodyWeightSpin->value());value.HeadWeight=float(_Ui->headWeightSpin->value());value.EyesWeight=float(_Ui->eyesWeightSpin->value());value.ClampWeight=float(_Ui->clampWeightSpin->value());
    _Ui->statusLabel->setText(animator->SetLookAt(value)?tr("临时 Look At 已设置。"):QString::fromStdString(animator->GetLastError()));
}

void AnimatorIKEditor::SetAvailable(bool available){_Ui->ikGroup->setEnabled(available);_Ui->requiresAvatarLabel->setVisible(!available);}

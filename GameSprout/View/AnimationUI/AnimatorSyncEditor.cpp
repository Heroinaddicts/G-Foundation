#include "AnimatorSyncEditor.h"
#include "ResourceField.h"
#include "ui_AnimatorSyncEditor.h"
#include <algorithm>
#include <cmath>

AnimatorSyncEditor::AnimatorSyncEditor(QWidget* parent):QWidget(parent),_Ui(new Ui::AnimatorSyncEditorForm){
    _Ui->setupUi(this);
    const auto change=[this]{if(!_Loading&&Changed)Changed();};
    connect(_Ui->syncSourceStateCombo,&QComboBox::currentIndexChanged,this,[this](int row){if(!_Loading)Select(row);});
    connect(_Ui->syncOverrideCheck,&QCheckBox::toggled,this,[this,change]{SetEnabled();change();});
    connect(_Ui->syncBlendParameterEdit,&QLineEdit::textEdited,this,change);
    connect(_Ui->syncMotionsTable,&QTableWidget::cellChanged,this,change);
    connect(_Ui->syncMotionsTable,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){
        if(_Loading||_State<0)return;std::string error;if(!Store(error))return;
        const auto found=std::find_if(_Overrides.begin(),_Overrides.end(),[&](const auto& value){return value.State==_States[_State].Name;});
        _Ui->syncMotionField->SetValue(found!=_Overrides.end()&&row>=0&&row<int(found->Motions.size())?found->Motions[row].Clip:GameSproutRuntime::ResourceReference{});
    });
    _Ui->syncClipField->Changed=[change](const auto&){change();};
    _Ui->syncMotionField->Changed=[this,change](const auto& reference){
        if(_Loading||_State<0)return;std::string error;if(!Store(error))return;
        const int row=_Ui->syncMotionsTable->currentRow();for(auto& item:_Overrides)if(item.State==_States[_State].Name&&row>=0&&row<int(item.Motions.size())){
            item.Motions[row].Clip=reference;_Ui->syncMotionsTable->item(row,2)->setText(reference.Guid.empty()?tr("未指定"):tr("Animation Clip"));change();}
    };
    connect(_Ui->syncAddMotionButton,&QPushButton::clicked,this,[this,change]{std::string error;if(!Store(error)||_State<0)return;
        for(auto& value:_Overrides)if(value.State==_States[_State].Name){GameSproutRuntime::BlendMotion motion;motion.Clip=_Ui->syncClipField->GetValue();motion.Threshold=value.Motions.empty()?0:value.Motions.back().Threshold+1;value.Motions.push_back(motion);Fill();change();break;}
    });
    connect(_Ui->syncRemoveMotionButton,&QPushButton::clicked,this,[this,change]{const int row=_Ui->syncMotionsTable->currentRow();std::string error;if(!Store(error)||_State<0||row<0)return;
        for(auto& value:_Overrides)if(value.State==_States[_State].Name&&row<int(value.Motions.size())){value.Motions.erase(value.Motions.begin()+row);Fill();change();break;}
    });
}
AnimatorSyncEditor::~AnimatorSyncEditor(){delete _Ui;}
void AnimatorSyncEditor::Bind(IAssetsManager* assets,ILibraryManager* library,ISelectionManager* selection){
    _Ui->syncClipField->Bind(assets,library,selection,eAssetType::AnimationClip);_Ui->syncMotionField->Bind(assets,library,selection,eAssetType::AnimationClip);
}
void AnimatorSyncEditor::Load(const GameSproutRuntime::AnimatorControllerData& data,int layer){
    _Loading=true;_States.clear();_Overrides.clear();_State=-1;_Ui->syncSourceStateCombo->clear();
    if(layer>=0&&layer<int(data.Layers.size())){
        _Overrides=data.Layers[layer].MotionOverrides;int graph=data.Layers[layer].SyncedLayerIndex;unsigned guard=0;
        while(graph>=0&&graph<int(data.Layers.size())&&guard++<data.Layers.size()){
            if(data.Layers[graph].SyncedLayerIndex<0){_States=data.Layers[graph].States;break;}graph=data.Layers[graph].SyncedLayerIndex;
        }
    }
    for(const auto& state:_States)_Ui->syncSourceStateCombo->addItem(QString::fromStdString(state.Name));
    _Loading=false;Select(_States.empty()?-1:0);
}
bool AnimatorSyncEditor::Store(std::string& error){
    if(_State<0||_State>=int(_States.size()))return true;const auto name=_States[_State].Name;
    auto found=std::find_if(_Overrides.begin(),_Overrides.end(),[&](const auto& value){return value.State==name;});
    if(!_Ui->syncOverrideCheck->isChecked()){if(found!=_Overrides.end())_Overrides.erase(found);return true;}
    GameSproutRuntime::AnimatorMotionOverride value;if(found!=_Overrides.end())value=*found;
    value.State=name;value.Clip=_Ui->syncClipField->GetValue();value.BlendParameter=_Ui->syncBlendParameterEdit->text().trimmed().toStdString();
    for(int row=0;row<_Ui->syncMotionsTable->rowCount();++row){bool a=false,b=false;
        const double threshold=_Ui->syncMotionsTable->item(row,0)->text().toDouble(&a),speed=_Ui->syncMotionsTable->item(row,1)->text().toDouble(&b);
        if(!a||!b||!std::isfinite(threshold)||!std::isfinite(speed)){error="同步动作的阈值和速度必须是数字。";return false;}
        value.Motions[row].Threshold=float(threshold);value.Motions[row].Speed=float(speed);
    }
    if(found!=_Overrides.end())*found=std::move(value);else _Overrides.push_back(std::move(value));return true;
}
void AnimatorSyncEditor::Select(int row){std::string error;if(!Store(error)){_Ui->syncStatusLabel->setText(QString::fromStdString(error));_Loading=true;_Ui->syncSourceStateCombo->setCurrentIndex(_State);_Loading=false;return;}_State=row;Fill();}
void AnimatorSyncEditor::Fill(){
    _Loading=true;const auto found=_State<0?_Overrides.end():std::find_if(_Overrides.begin(),_Overrides.end(),[&](const auto& value){return value.State==_States[_State].Name;});
    _Ui->syncOverrideCheck->setChecked(found!=_Overrides.end());_Ui->syncClipField->SetValue(found!=_Overrides.end()?found->Clip:GameSproutRuntime::ResourceReference{});
    _Ui->syncBlendParameterEdit->setText(found!=_Overrides.end()?QString::fromStdString(found->BlendParameter):QString());
    _Ui->syncMotionsTable->setRowCount(found!=_Overrides.end()?int(found->Motions.size()):0);
    if(found!=_Overrides.end())for(std::size_t row=0;row<found->Motions.size();++row){const auto& motion=found->Motions[row];
        _Ui->syncMotionsTable->setItem(int(row),0,new QTableWidgetItem(QString::number(motion.Threshold)));_Ui->syncMotionsTable->setItem(int(row),1,new QTableWidgetItem(QString::number(motion.Speed)));
        auto* label=new QTableWidgetItem(motion.Clip.Guid.empty()?tr("未指定"):tr("Animation Clip"));label->setFlags(label->flags()&~Qt::ItemIsEditable);_Ui->syncMotionsTable->setItem(int(row),2,label);
    }
    _Ui->syncMotionField->SetValue({});SetEnabled();_Loading=false;
}
void AnimatorSyncEditor::SetEnabled(){const bool enabled=_State>=0&&_Ui->syncOverrideCheck->isChecked();
    for(auto* item:std::vector<QWidget*>{_Ui->syncClipField,_Ui->syncBlendParameterEdit,_Ui->syncMotionsTable,_Ui->syncAddMotionButton,_Ui->syncRemoveMotionButton,_Ui->syncMotionField})item->setEnabled(enabled);
}
bool AnimatorSyncEditor::Read(GameSproutRuntime::AnimatorLayer& layer,std::string& error){if(!Store(error))return false;layer.MotionOverrides=_Overrides;return true;}

void AnimatorSyncEditor::RenameParameter(const std::string& from,const std::string& to){
    for(auto& value:_Overrides)if(value.BlendParameter==from)value.BlendParameter=to;
    if(_Ui->syncBlendParameterEdit->text().toStdString()==from)_Ui->syncBlendParameterEdit->setText(QString::fromStdString(to));
}

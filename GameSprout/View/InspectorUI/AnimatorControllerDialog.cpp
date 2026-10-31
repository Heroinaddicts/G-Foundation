#include "AnimatorControllerDialog.h"
#include "IRuntimeAssets.h"
#include "ResourceField.h"
#include "AnimatorSyncEditor.h"
#include "ui_AnimatorControllerDialog.h"
#include "ui_AnimatorDiscardDialog.h"
#include <QCloseEvent>
#include <QTableWidget>
#include <QSignalBlocker>
#include <QHeaderView>
#include <cmath>
#include <algorithm>
#include <map>

namespace {
QString Cell(QTableWidget* table,int row,int column){auto* item=table->item(row,column);return item?item->text().trimmed():QString();}
void Text(QTableWidget* table,int row,int column,const QString& text,bool editable=true){auto* item=new QTableWidgetItem(text);if(!editable)item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(row,column,item);}
void Flag(QTableWidget* table,int row,int column,bool value){auto* item=new QTableWidgetItem;item->setFlags((item->flags()|Qt::ItemIsUserCheckable)&~Qt::ItemIsEditable);item->setCheckState(value?Qt::Checked:Qt::Unchecked);table->setItem(row,column,item);}
bool Checked(QTableWidget* table,int row,int column){auto* item=table->item(row,column);return item&&item->checkState()==Qt::Checked;}
const char* TypeName(GameSproutRuntime::AnimatorParameterType type){switch(type){case GameSproutRuntime::AnimatorParameterType::Float:return "Float";case GameSproutRuntime::AnimatorParameterType::Int:return "Int";case GameSproutRuntime::AnimatorParameterType::Bool:return "Bool";case GameSproutRuntime::AnimatorParameterType::Trigger:return "Trigger";}return "";}
const char* CompareName(GameSproutRuntime::AnimatorCompare compare){switch(compare){case GameSproutRuntime::AnimatorCompare::Greater:return "Greater";case GameSproutRuntime::AnimatorCompare::Less:return "Less";case GameSproutRuntime::AnimatorCompare::Equal:return "Equal";case GameSproutRuntime::AnimatorCompare::NotEqual:return "NotEqual";case GameSproutRuntime::AnimatorCompare::If:return "If";case GameSproutRuntime::AnimatorCompare::IfNot:return "IfNot";}return "";}
bool Compare(const QString& text,GameSproutRuntime::AnimatorCompare& value){for(unsigned index=0;index<=5;++index){auto candidate=static_cast<GameSproutRuntime::AnimatorCompare>(index);if(text==CompareName(candidate)){value=candidate;return true;}}return false;}
bool Number(QTableWidget* table,int row,int column,double& number){bool ok=false;number=Cell(table,row,column).toDouble(&ok);return ok&&std::isfinite(number);}
template<typename Value>bool NameInUse(const std::vector<Value>& values,const std::string& name,int except=-1){
    for(std::size_t index=0;index<values.size();++index)if(int(index)!=except&&values[index].Name==name)return true;return false;
}
template<typename Value>std::string UniqueName(const std::vector<Value>& values,const char* prefix){
    unsigned suffix=1;std::string name;do{name=std::string(prefix)+std::to_string(suffix++);}while(NameInUse(values,name));return name;
}
}
AnimatorControllerDialog::AnimatorControllerDialog(IRuntimeAssets* resources,IAssetsManager* assets,ILibraryManager* library,
    ISelectionManager* selection,GameSproutRuntime::ResourceReference reference,QWidget* parent)
    :QDialog(parent),_Ui(new Ui::AnimatorControllerDialogForm),_Resources(resources),_Assets(assets),_Library(library),_Selection(selection),_Reference(std::move(reference)){
    _Ui->setupUi(this);
    _Ui->syncEditor->Bind(assets,library,selection);
    _Ui->syncEditor->Changed=[this]{if(!_Filling)_Dirty=true;};
    connect(_Ui->syncSourceCombo,&QComboBox::currentIndexChanged,this,[this](int row){if(!_Filling)SetSyncSource(_Ui->syncSourceCombo->itemData(row).toInt());});
    for(auto* check:{_Ui->syncTimingCheck,_Ui->ikPassCheck})connect(check,&QCheckBox::toggled,this,[this]{if(!_Filling)_Dirty=true;});
    _Ui->clipField->Bind(assets,library,selection,eAssetType::AnimationClip);
    _Ui->motionClipField->Bind(assets,library,selection,eAssetType::AnimationClip);
    _Ui->layerMaskField->Bind(assets,library,selection,eAssetType::AvatarMask);
    _Ui->referenceClipField->Bind(assets,library,selection,eAssetType::AnimationClip);
    connect(_Ui->reloadButton,&QPushButton::clicked,this,[this]{Reload();});
    connect(_Ui->saveButton,&QPushButton::clicked,this,[this]{Save();});
    connect(_Ui->closeButton,&QPushButton::clicked,this,&QDialog::reject);
    connect(_Ui->statesTable,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(!_Filling)SelectState(row);});
    connect(_Ui->transitionsTable,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(!_Filling)SelectTransition(row);});
    connect(_Ui->motionsTable,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(!_Filling)SelectMotion(row);});
    connect(_Ui->statesTable,&QTableWidget::cellChanged,this,[this](int row,int column){
        if(_Filling||column!=0||row<0||row>=static_cast<int>(Layer().States.size()))return;
        const QString previous=QString::fromStdString(Layer().States[row].Name),name=Cell(_Ui->statesTable,row,0);
        if(name.isEmpty()||name=="AnyState"||name.toUtf8().size()>256||NameInUse(Layer().States,name.toStdString(),row)){
            const QSignalBlocker blocker(_Ui->statesTable);_Ui->statesTable->item(row,0)->setText(previous);
            Error("状态名称不能为空、重复、超过 256 字节，或使用保留名 AnyState。");return;
        }
        {const QSignalBlocker blocker(_Ui->statesTable);_Ui->statesTable->item(row,0)->setText(name);}
        if(name==previous)return;_Dirty=true;Error({});
        for(std::size_t index=0;index<_Data.Layers.size();++index)if(GraphIndex(int(index))==_LayerRow)for(auto& motion:_Data.Layers[index].MotionOverrides)if(motion.State==previous.toStdString())motion.State=name.toStdString();
        Layer().States[row].Name=name.toStdString();_Ui->defaultStateCombo->setItemText(row,name);
        for(int transition=0;transition<_Ui->transitionsTable->rowCount();++transition)
            for(int slot=0;slot<2;++slot)if(Cell(_Ui->transitionsTable,transition,slot)==previous)Text(_Ui->transitionsTable,transition,slot,name);
    });
    connect(_Ui->parametersTable,&QTableWidget::cellChanged,this,[this](int row,int column){
        if(_Filling||column!=0||row<0||row>=static_cast<int>(_Data.Parameters.size()))return;
        const auto previous=_Data.Parameters[row].Name,name=Cell(_Ui->parametersTable,row,0).toStdString();
        if(name.empty()||name.size()>256||NameInUse(_Data.Parameters,name,row)){
            const QSignalBlocker blocker(_Ui->parametersTable);_Ui->parametersTable->item(row,0)->setText(QString::fromStdString(previous));
            Error("参数名称不能为空、重复或超过 256 字节。");return;
        }
        {const QSignalBlocker blocker(_Ui->parametersTable);_Ui->parametersTable->item(row,0)->setText(QString::fromStdString(name));}
        if(name==previous)return;_Dirty=true;Error({});
        for(auto& backup:_IndependentDrafts){for(auto& state:backup.second.States)if(state.BlendParameter==previous)state.BlendParameter=name;for(auto& transition:backup.second.Transitions)for(auto& condition:transition.Conditions)if(condition.Parameter==previous)condition.Parameter=name;}
        _Data.Parameters[row].Name=name;_Ui->syncEditor->RenameParameter(previous,name);
        for(auto& layer:_Data.Layers){
            for(auto& state:layer.States)if(state.BlendParameter==previous)state.BlendParameter=name;
            for(auto& transition:layer.Transitions)for(auto& condition:transition.Conditions)if(condition.Parameter==previous)condition.Parameter=name;
            for(auto& motion:layer.MotionOverrides)if(motion.BlendParameter==previous)motion.BlendParameter=name;
        }
        if(_Ui->blendParameterEdit->text().toStdString()==previous)_Ui->blendParameterEdit->setText(QString::fromStdString(name));
        for(int condition=0;condition<_Ui->conditionsTable->rowCount();++condition)
            if(Cell(_Ui->conditionsTable,condition,0).toStdString()==previous)Text(_Ui->conditionsTable,condition,0,QString::fromStdString(name));
    });
    _Ui->clipField->Changed=[this](const GameSproutRuntime::ResourceReference& ref){if(!_Filling&&_StateRow>=0){Layer().States[_StateRow].Clip=ref;Text(_Ui->statesTable,_StateRow,1,ClipName(ref),false);}};
    _Ui->motionClipField->Changed=[this](const GameSproutRuntime::ResourceReference& ref){if(!_Filling&&_StateRow>=0&&_MotionRow>=0){Layer().States[_StateRow].Motions[_MotionRow].Clip=ref;Text(_Ui->motionsTable,_MotionRow,2,ClipName(ref),false);}};
    const auto draft=[this](){GameSproutRuntime::AnimatorControllerData value;std::string error;if(!Collect(value,error)){Error(error);return false;}_Data=std::move(value);return true;};
    connect(_Ui->addStateButton,&QPushButton::clicked,this,[this,draft]{if(!draft())return;GameSproutRuntime::AnimatorState state;state.Name=UniqueName(Layer().States,"State ");Layer().States.push_back(state);Fill();_Ui->statesTable->setCurrentCell(static_cast<int>(Layer().States.size())-1,0);});
    connect(_Ui->removeStateButton,&QPushButton::clicked,this,[this,draft]{const int row=_Ui->statesTable->currentRow();if(row<0||!draft())return;
        const auto removedName=Layer().States[row].Name;for(std::size_t index=0;index<_Data.Layers.size();++index)if(GraphIndex(int(index))==_LayerRow)std::erase_if(_Data.Layers[index].MotionOverrides,[&](const auto& value){return value.State==removedName;});
        Layer().States.erase(Layer().States.begin()+row);std::erase_if(Layer().Transitions,[row](const auto& transition){return transition.From==row||transition.To==static_cast<unsigned>(row);});
        for(auto& transition:Layer().Transitions){if(transition.From>row)--transition.From;if(transition.To>static_cast<unsigned>(row))--transition.To;}
        if(Layer().DefaultState==static_cast<unsigned>(row))Layer().DefaultState=0;else if(Layer().DefaultState>static_cast<unsigned>(row))--Layer().DefaultState;Fill();});
    connect(_Ui->addParameterButton,&QPushButton::clicked,this,[this,draft]{if(!draft())return;_Data.Parameters.push_back({UniqueName(_Data.Parameters,"Parameter "),GameSproutRuntime::AnimatorParameterType::Float,0});Fill();});
    connect(_Ui->removeParameterButton,&QPushButton::clicked,this,[this,draft]{const int row=_Ui->parametersTable->currentRow();if(row<0||!draft())return;const auto name=_Data.Parameters[row].Name;
        for(const auto& layer:_Data.Layers){for(const auto& motion:layer.MotionOverrides)if(motion.BlendParameter==name){Error("该参数仍被同步层动作引用，请先解除引用。");return;}for(const auto& state:layer.States)if(state.BlendParameter==name){Error("该参数仍被混合状态引用，请先解除引用。");return;}
            for(const auto& transition:layer.Transitions)for(const auto& condition:transition.Conditions)if(condition.Parameter==name){Error("该参数仍被转场条件引用，请先解除引用。");return;}}
        _Data.Parameters.erase(_Data.Parameters.begin()+row);Fill();});
    connect(_Ui->addTransitionButton,&QPushButton::clicked,this,[this,draft]{if(!draft()||Layer().States.empty())return;GameSproutRuntime::AnimatorTransition transition;transition.From=0;transition.To=Layer().States.size()>1?1:0;transition.HasExitTime=true;transition.CanTransitionToSelf=Layer().States.size()==1;Layer().Transitions.push_back(transition);Fill();_Ui->transitionsTable->setCurrentCell(static_cast<int>(Layer().Transitions.size())-1,0);});
    connect(_Ui->removeTransitionButton,&QPushButton::clicked,this,[this,draft]{const int row=_Ui->transitionsTable->currentRow();if(row<0||!draft())return;Layer().Transitions.erase(Layer().Transitions.begin()+row);Fill();});
    connect(_Ui->addMotionButton,&QPushButton::clicked,this,[this]{if(_StateRow<0||!StoreStateDetail())return;auto& state=Layer().States[_StateRow];GameSproutRuntime::BlendMotion motion;motion.Threshold=state.Motions.empty()?0:state.Motions.back().Threshold+1;motion.Clip=state.Clip;state.Motions.push_back(motion);const int row=_StateRow;_StateRow=-1;SelectState(row);});
    connect(_Ui->removeMotionButton,&QPushButton::clicked,this,[this]{if(_StateRow<0||_MotionRow<0||!StoreStateDetail())return;auto& motions=Layer().States[_StateRow].Motions;motions.erase(motions.begin()+_MotionRow);const int row=_StateRow;_StateRow=-1;SelectState(row);});
    connect(_Ui->addConditionButton,&QPushButton::clicked,this,[this]{if(_TransitionRow<0||!StoreConditions())return;GameSproutRuntime::AnimatorCondition condition;if(!_Data.Parameters.empty()){condition.Parameter=_Data.Parameters.front().Name;const auto type=_Data.Parameters.front().Type;if(type==GameSproutRuntime::AnimatorParameterType::Bool||type==GameSproutRuntime::AnimatorParameterType::Trigger)condition.Compare=GameSproutRuntime::AnimatorCompare::If;}Layer().Transitions[_TransitionRow].Conditions.push_back(condition);const int row=_TransitionRow;_TransitionRow=-1;SelectTransition(row);});
    connect(_Ui->removeConditionButton,&QPushButton::clicked,this,[this]{const int row=_Ui->conditionsTable->currentRow();if(_TransitionRow<0||row<0||!StoreConditions())return;auto& conditions=Layer().Transitions[_TransitionRow].Conditions;conditions.erase(conditions.begin()+row);const int selected=_TransitionRow;_TransitionRow=-1;SelectTransition(selected);});
    for(auto* table:{_Ui->statesTable,_Ui->parametersTable,_Ui->transitionsTable,_Ui->conditionsTable,_Ui->motionsTable})
        connect(table,&QTableWidget::cellChanged,this,[this,table](int,int column){
            if(!_Filling&&!((table==_Ui->statesTable||table==_Ui->parametersTable)&&column==0))_Dirty=true;
        });
    connect(_Ui->blendParameterEdit,&QLineEdit::textEdited,this,[this]{if(!_Filling)_Dirty=true;});
    connect(_Ui->defaultStateCombo,&QComboBox::currentIndexChanged,this,[this](int){if(!_Filling)_Dirty=true;});
    for(auto* button:{_Ui->addStateButton,_Ui->removeStateButton,_Ui->addParameterButton,_Ui->removeParameterButton,
        _Ui->addTransitionButton,_Ui->removeTransitionButton,_Ui->addMotionButton,_Ui->removeMotionButton,_Ui->addConditionButton,_Ui->removeConditionButton})
        connect(button,&QPushButton::clicked,this,[this]{_Dirty=true;});
    connect(_Ui->layerCombo,&QComboBox::currentIndexChanged,this,[this](int row){if(!_Filling)SelectLayer(row);});
    connect(_Ui->addLayerButton,&QPushButton::clicked,this,[this,draft]{
        if(!draft())return;GameSproutRuntime::AnimatorLayer layer;unsigned suffix=1;
        do{layer.Name="Layer "+std::to_string(suffix++);}while(std::any_of(_Data.Layers.begin(),_Data.Layers.end(),[&](const auto& value){return value.Name==layer.Name;}));
        GameSproutRuntime::AnimatorState state;state.Name="Idle";layer.States.push_back(state);_Data.Layers.push_back(layer);_LayerRow=int(_Data.Layers.size())-1;_Dirty=true;Fill();
    });
    connect(_Ui->removeLayerButton,&QPushButton::clicked,this,[this,draft]{if(_LayerRow<=0||!draft())return;for(const auto& layer:_Data.Layers)if(layer.SyncedLayerIndex==_LayerRow){Error("其他同步层仍引用此层，请先解除同步。");return;}const int removed=_LayerRow;_Data.Layers.erase(_Data.Layers.begin()+removed);for(auto& layer:_Data.Layers)if(layer.SyncedLayerIndex>removed)--layer.SyncedLayerIndex;_IndependentDrafts.clear();--_LayerRow;_Dirty=true;Fill();});
    connect(_Ui->moveLayerUpButton,&QPushButton::clicked,this,[this,draft]{if(_LayerRow<=1||!draft())return;RemapSyncIndices(_LayerRow,_LayerRow-1);std::swap(_Data.Layers[_LayerRow],_Data.Layers[_LayerRow-1]);--_LayerRow;_Dirty=true;Fill();});
    connect(_Ui->moveLayerDownButton,&QPushButton::clicked,this,[this,draft]{if(_LayerRow<=0||_LayerRow+1>=int(_Data.Layers.size())||!draft())return;RemapSyncIndices(_LayerRow,_LayerRow+1);std::swap(_Data.Layers[_LayerRow],_Data.Layers[_LayerRow+1]);++_LayerRow;_Dirty=true;Fill();});
    connect(_Ui->layerNameEdit,&QLineEdit::textEdited,this,[this]{if(!_Filling)_Dirty=true;});
    connect(_Ui->layerModeCombo,&QComboBox::currentIndexChanged,this,[this](int){if(!_Filling){_Dirty=true;UpdateLayerControls();}});
    for(auto* spin:{_Ui->layerWeightSpin,_Ui->referenceTimeSpin})connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(!_Filling)_Dirty=true;});
    _Ui->layerMaskField->Changed=[this](const auto&){if(!_Filling)_Dirty=true;};
    _Ui->referenceClipField->Changed=[this](const auto&){if(!_Filling)_Dirty=true;};
    Reload();
}
AnimatorControllerDialog::~AnimatorControllerDialog(){_Ui->clipField->Changed={};_Ui->motionClipField->Changed={};_Ui->layerMaskField->Changed={};_Ui->referenceClipField->Changed={};delete _Ui;}
void AnimatorControllerDialog::Error(const std::string& error){_Ui->statusLabel->setText(QString::fromStdString(error));}
QString AnimatorControllerDialog::ClipName(const GameSproutRuntime::ResourceReference& reference) const{
    if(reference.Guid.empty())return QStringLiteral("未指定动作");AssetReferenceCandidate candidate;std::string error;
    if(AssetReferenceResolver::Resolve(_Assets,_Library,reference,eAssetType::AnimationClip,candidate,error,false))return QString::fromStdString(candidate.Name);
    return QStringLiteral("动作已丢失");
}
void AnimatorControllerDialog::Reload(){std::string error;
    if(_Dirty&&!ConfirmDiscard())return;
    _Ui->layerGroup->setEnabled(false);_Ui->tabs->setEnabled(false);
    if(!_Resources||!_Resources->LoadAnimatorControllerSource(_Reference,_Data,_Snapshot,error)){Error(error.empty()?"Controller 资源服务不可用。":error);_Ui->saveButton->setEnabled(false);return;}
    if(_Data.Layers.empty()){Error("Controller 必须包含基础层。");_Ui->saveButton->setEnabled(false);return;}
    _Ui->layerGroup->setEnabled(true);_Ui->tabs->setEnabled(true);
    _IndependentDrafts.clear();
    _LayerRow=std::clamp(_LayerRow,0,int(_Data.Layers.size())-1);_Ui->saveButton->setEnabled(true);Error({});Fill();_Dirty=false;
}
void AnimatorControllerDialog::Fill(){
    _Filling=true;_StateRow=_TransitionRow=_MotionRow=-1;
    _Ui->layerCombo->clear();for(const auto& layer:_Data.Layers)_Ui->layerCombo->addItem(QString::fromStdString(layer.Name));
    _Ui->layerCombo->setCurrentIndex(_LayerRow);_Ui->layerNameEdit->setText(QString::fromStdString(Layer().Name));
    _Ui->layerModeCombo->setCurrentIndex(int(Layer().BlendingMode));_Ui->layerWeightSpin->setValue(Layer().DefaultWeight);
    _Ui->layerMaskField->SetValue(Layer().Mask);_Ui->referenceClipField->SetValue(Layer().ReferenceClip);_Ui->referenceTimeSpin->setValue(Layer().ReferenceTime);
    _Ui->syncSourceCombo->clear();_Ui->syncSourceCombo->addItem(tr("独立状态机"),-1);
    for(std::size_t index=0;index<_Data.Layers.size();++index)if(int(index)!=_LayerRow&&GraphIndex(int(index))!=_LayerRow)_Ui->syncSourceCombo->addItem(QString::fromStdString(_Data.Layers[index].Name),int(index));
    _Ui->syncSourceCombo->setCurrentIndex(_Ui->syncSourceCombo->findData(Layer().SyncedLayerIndex));
    _Ui->syncTimingCheck->setChecked(Layer().SyncedLayerAffectsTiming);_Ui->ikPassCheck->setChecked(Layer().IKPass);
    _Ui->syncEditor->Load(_Data,_LayerRow);UpdateLayerControls();
    _Ui->statesTable->setRowCount(static_cast<int>(Layer().States.size()));_Ui->defaultStateCombo->clear();
    for(std::size_t index=0;index<Layer().States.size();++index){const auto& state=Layer().States[index];const int row=static_cast<int>(index);
        Text(_Ui->statesTable,row,0,QString::fromStdString(state.Name));Text(_Ui->statesTable,row,1,ClipName(state.Clip),false);Text(_Ui->statesTable,row,2,QString::number(state.Speed));Flag(_Ui->statesTable,row,3,state.Loop);_Ui->defaultStateCombo->addItem(QString::fromStdString(state.Name));}
    _Ui->defaultStateCombo->setCurrentIndex(static_cast<int>(Layer().DefaultState));
    _Ui->parametersTable->setRowCount(static_cast<int>(_Data.Parameters.size()));
    for(std::size_t index=0;index<_Data.Parameters.size();++index){const auto& parameter=_Data.Parameters[index];Text(_Ui->parametersTable,index,0,QString::fromStdString(parameter.Name));Text(_Ui->parametersTable,index,1,TypeName(parameter.Type));Text(_Ui->parametersTable,index,2,QString::number(parameter.DefaultValue,'g',12));}
    _Ui->transitionsTable->setRowCount(static_cast<int>(Layer().Transitions.size()));
    for(std::size_t index=0;index<Layer().Transitions.size();++index){const auto& transition=Layer().Transitions[index];
        Text(_Ui->transitionsTable,index,0,transition.From<0?QStringLiteral("AnyState"):QString::fromStdString(Layer().States[transition.From].Name));
        Text(_Ui->transitionsTable,index,1,QString::fromStdString(Layer().States[transition.To].Name));Text(_Ui->transitionsTable,index,2,QString::number(transition.Duration));
        Flag(_Ui->transitionsTable,index,3,transition.HasExitTime);Text(_Ui->transitionsTable,index,4,QString::number(transition.ExitTime));Text(_Ui->transitionsTable,index,5,QString::number(transition.TargetOffset));Flag(_Ui->transitionsTable,index,6,transition.CanTransitionToSelf);}
    _Filling=false;SelectState(Layer().States.empty()?-1:0);SelectTransition(Layer().Transitions.empty()?-1:0);
    if(!Layer().States.empty())_Ui->statesTable->setCurrentCell(0,0);if(!Layer().Transitions.empty())_Ui->transitionsTable->setCurrentCell(0,0);
}
bool AnimatorControllerDialog::StoreStateDetail(){
    if(_StateRow<0)return true;auto& state=Layer().States[_StateRow];std::vector<GameSproutRuntime::BlendMotion> motions=state.Motions;
    for(int row=0;row<_Ui->motionsTable->rowCount();++row){double threshold,speed;
        if(!Number(_Ui->motionsTable,row,0,threshold)||!Number(_Ui->motionsTable,row,1,speed)){Error("子动作阈值和速度必须是有效数字。");return false;}
        motions[row].Threshold=static_cast<float>(threshold);motions[row].Speed=static_cast<float>(speed);}
    state.BlendParameter=_Ui->blendParameterEdit->text().trimmed().toStdString();state.Motions=std::move(motions);return true;
}
void AnimatorControllerDialog::SelectState(int row){
    if(row==_StateRow)return;if(!StoreStateDetail())return;_StateRow=row>=0&&row<static_cast<int>(Layer().States.size())?row:-1;_MotionRow=-1;_Filling=true;
    _Ui->clipField->SetValue(_StateRow>=0?Layer().States[_StateRow].Clip:GameSproutRuntime::ResourceReference{});_Ui->blendParameterEdit->setText(_StateRow>=0?QString::fromStdString(Layer().States[_StateRow].BlendParameter):QString());
    const std::size_t count=_StateRow>=0?Layer().States[_StateRow].Motions.size():0;_Ui->motionsTable->setRowCount(static_cast<int>(count));
    for(std::size_t index=0;index<count;++index){const auto& motion=Layer().States[_StateRow].Motions[index];Text(_Ui->motionsTable,index,0,QString::number(motion.Threshold));Text(_Ui->motionsTable,index,1,QString::number(motion.Speed));Text(_Ui->motionsTable,index,2,ClipName(motion.Clip),false);}
    _Ui->clipField->setEnabled(_StateRow>=0);_Ui->blendParameterEdit->setEnabled(_StateRow>=0);_Filling=false;SelectMotion(count?0:-1);
}
void AnimatorControllerDialog::SelectMotion(int row){_MotionRow=_StateRow>=0&&row>=0&&row<static_cast<int>(Layer().States[_StateRow].Motions.size())?row:-1;_Filling=true;_Ui->motionClipField->SetValue(_MotionRow>=0?Layer().States[_StateRow].Motions[_MotionRow].Clip:GameSproutRuntime::ResourceReference{});_Ui->motionClipField->setEnabled(_MotionRow>=0);_Filling=false;}
bool AnimatorControllerDialog::StoreConditions(){
    if(_TransitionRow<0)return true;std::vector<GameSproutRuntime::AnimatorCondition> conditions;
    for(int row=0;row<_Ui->conditionsTable->rowCount();++row){GameSproutRuntime::AnimatorCondition condition;condition.Parameter=Cell(_Ui->conditionsTable,row,0).toStdString();
        if(!Compare(Cell(_Ui->conditionsTable,row,1),condition.Compare)||!Number(_Ui->conditionsTable,row,2,condition.Value)){Error("条件的比较方式或数值无效。");return false;}conditions.push_back(std::move(condition));}
    Layer().Transitions[_TransitionRow].Conditions=std::move(conditions);return true;
}
void AnimatorControllerDialog::SelectTransition(int row){
    if(row==_TransitionRow)return;if(!StoreConditions())return;_TransitionRow=row>=0&&row<static_cast<int>(Layer().Transitions.size())?row:-1;_Filling=true;
    const std::size_t count=_TransitionRow>=0?Layer().Transitions[_TransitionRow].Conditions.size():0;_Ui->conditionsTable->setRowCount(static_cast<int>(count));
    for(std::size_t index=0;index<count;++index){const auto& condition=Layer().Transitions[_TransitionRow].Conditions[index];Text(_Ui->conditionsTable,index,0,QString::fromStdString(condition.Parameter));Text(_Ui->conditionsTable,index,1,CompareName(condition.Compare));Text(_Ui->conditionsTable,index,2,QString::number(condition.Value,'g',12));}_Filling=false;
}
bool AnimatorControllerDialog::Collect(GameSproutRuntime::AnimatorControllerData& data,std::string& error){
    if(!StoreStateDetail()||!StoreConditions()){error="请修正状态或条件中的输入。";return false;}data=_Data;
    auto& layer=data.Layers[_LayerRow];
    layer.IKPass=_Ui->ikPassCheck->isChecked();layer.SyncedLayerAffectsTiming=layer.SyncedLayerIndex>=0&&_Ui->syncTimingCheck->isChecked();
    if(layer.SyncedLayerIndex>=0&&!_Ui->syncEditor->Read(layer,error))return false;
    layer.Name=_Ui->layerNameEdit->text().trimmed().toStdString();
    layer.BlendingMode=static_cast<GameSproutRuntime::AnimatorLayerBlendingMode>(_Ui->layerModeCombo->currentIndex());
    layer.DefaultWeight=float(_Ui->layerWeightSpin->value());layer.Mask=_Ui->layerMaskField->GetValue();
    layer.ReferenceClip=_Ui->referenceClipField->GetValue();layer.ReferenceTime=float(_Ui->referenceTimeSpin->value());
    std::map<std::string,unsigned> names;
    for(int row=0;row<_Ui->statesTable->rowCount();++row){auto& state=layer.States[row];double speed;
        state.Name=Cell(_Ui->statesTable,row,0).toStdString();names.emplace(state.Name,row);
        if(!Number(_Ui->statesTable,row,2,speed)){error="状态速度必须是数字。";return false;}state.Speed=static_cast<float>(speed);state.Loop=Checked(_Ui->statesTable,row,3);}
    layer.DefaultState=_Ui->defaultStateCombo->currentIndex()<0?0:static_cast<unsigned>(_Ui->defaultStateCombo->currentIndex());
    for(int row=0;row<_Ui->parametersTable->rowCount();++row){auto& parameter=data.Parameters[row];parameter.Name=Cell(_Ui->parametersTable,row,0).toStdString();
        const auto type=Cell(_Ui->parametersTable,row,1);bool found=false;for(unsigned index=0;index<4;++index){const auto value=static_cast<GameSproutRuntime::AnimatorParameterType>(index);if(type==TypeName(value)){parameter.Type=value;found=true;}}
        if(!found||!Number(_Ui->parametersTable,row,2,parameter.DefaultValue)){error="参数类型或默认值无效。";return false;}}
    for(int row=0;row<_Ui->transitionsTable->rowCount();++row){auto& transition=layer.Transitions[row];const auto from=Cell(_Ui->transitionsTable,row,0).toStdString(),to=Cell(_Ui->transitionsTable,row,1).toStdString();
        if((from!="AnyState"&&!names.count(from))||!names.count(to)){error="转场必须引用表格中已有的状态名称。";return false;}transition.From=from=="AnyState"?-1:static_cast<int>(names.at(from));transition.To=names.at(to);
        double duration,exit,offset;if(!Number(_Ui->transitionsTable,row,2,duration)||!Number(_Ui->transitionsTable,row,4,exit)||!Number(_Ui->transitionsTable,row,5,offset)){error="转场时间必须是有效数字。";return false;}
        transition.Duration=static_cast<float>(duration);transition.ExitTime=static_cast<float>(exit);transition.TargetOffset=static_cast<float>(offset);transition.HasExitTime=Checked(_Ui->transitionsTable,row,3);transition.CanTransitionToSelf=Checked(_Ui->transitionsTable,row,6);}
    return true;
}
void AnimatorControllerDialog::Save(){GameSproutRuntime::AnimatorControllerData data;std::string error;
    if(!_Resources||_Data.Layers.empty()){Error("Controller 源数据尚未加载。");return;}
    if(!Collect(data,error)||!_Resources->SaveAnimatorControllerSource(_Reference,data,_Snapshot,error)){Error(error);return;}
    _Dirty=false;Reload();Error("已保存并提交资源导入。");
}
bool AnimatorControllerDialog::ConfirmDiscard(){
    if(!_Dirty)return true;
    QDialog dialog(this);Ui::AnimatorDiscardDialogForm form;form.setupUi(&dialog);
    connect(form.cancelButton,&QPushButton::clicked,&dialog,&QDialog::reject);
    connect(form.discardButton,&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(form.saveButton,&QPushButton::clicked,&dialog,[this,&dialog,&form]{Save();if(!_Dirty)dialog.accept();else form.errorLabel->setText(_Ui->statusLabel->text());});
    return dialog.exec()==QDialog::Accepted;
}
void AnimatorControllerDialog::reject(){if(!ConfirmDiscard())return;_Dirty=false;QDialog::reject();}
void AnimatorControllerDialog::closeEvent(QCloseEvent* event){if(!ConfirmDiscard()){event->ignore();return;}_Dirty=false;QDialog::closeEvent(event);}

GameSproutRuntime::AnimatorLayer& AnimatorControllerDialog::Layer(){return _Data.Layers[_LayerRow];}
void AnimatorControllerDialog::SelectLayer(int row){
    if(row<0||row==_LayerRow)return;GameSproutRuntime::AnimatorControllerData data;std::string error;
    if(!Collect(data,error)){Error(error);_Filling=true;_Ui->layerCombo->setCurrentIndex(_LayerRow);_Filling=false;return;}
    _Data=std::move(data);_LayerRow=row;Fill();
}
void AnimatorControllerDialog::UpdateLayerControls(){
    const bool synced=Layer().SyncedLayerIndex>=0;
    _Ui->syncSourceCombo->setEnabled(_LayerRow>0);_Ui->syncTimingCheck->setEnabled(synced);
    _Ui->tabs->setTabEnabled(_Ui->tabs->indexOf(_Ui->statesTab),!synced);_Ui->tabs->setTabEnabled(_Ui->tabs->indexOf(_Ui->transitionsTab),!synced);
    _Ui->tabs->setTabEnabled(_Ui->tabs->indexOf(_Ui->syncTab),synced);
    if(synced&&(_Ui->tabs->currentWidget()==_Ui->statesTab||_Ui->tabs->currentWidget()==_Ui->transitionsTab))_Ui->tabs->setCurrentWidget(_Ui->syncTab);
    const bool extra=_LayerRow>0,additive=extra&&_Ui->layerModeCombo->currentIndex()==int(GameSproutRuntime::AnimatorLayerBlendingMode::Additive);
    _Ui->layerModeCombo->setEnabled(extra);_Ui->layerWeightSpin->setEnabled(extra);_Ui->layerMaskField->setEnabled(extra);
    _Ui->referenceClipField->setEnabled(additive);_Ui->referenceTimeSpin->setEnabled(additive);
    _Ui->removeLayerButton->setEnabled(extra);_Ui->moveLayerUpButton->setEnabled(_LayerRow>1);_Ui->moveLayerDownButton->setEnabled(extra&&_LayerRow+1<int(_Data.Layers.size()));
    _Ui->layerHelpLabel->setText(extra?tr("由上到下混合；权重保存到 Controller。未指定 Mask 表示全身。Additive 未指定参考动作时使用骨架初始姿态。"):
        tr("基础层固定 Override、权重 1，不使用 Mask 和叠加参考姿势；基础层不可删除或移动。"));
    if(!additive){_Ui->referenceClipField->SetValue({});_Ui->referenceTimeSpin->setValue(0);}
}

int AnimatorControllerDialog::GraphIndex(int layer)const{
    for(std::size_t guard=0;guard<_Data.Layers.size();++guard){if(layer<0||layer>=int(_Data.Layers.size()))return -1;if(_Data.Layers[layer].SyncedLayerIndex<0)return layer;layer=_Data.Layers[layer].SyncedLayerIndex;}return -1;
}
void AnimatorControllerDialog::SetSyncSource(int source){
    GameSproutRuntime::AnimatorControllerData value;std::string error;if(!Collect(value,error)){Error(error);Fill();return;}_Data=std::move(value);
    if(source==Layer().SyncedLayerIndex)return;
    if(source>=0){if(Layer().SyncedLayerIndex<0)_IndependentDrafts[_LayerRow]=Layer();Layer().States.clear();Layer().Transitions.clear();Layer().DefaultState=0;Layer().SyncedLayerIndex=source;Layer().MotionOverrides.clear();}
    else{const auto backup=_IndependentDrafts.find(_LayerRow);if(backup!=_IndependentDrafts.end()){Layer().States=backup->second.States;Layer().Transitions=backup->second.Transitions;Layer().DefaultState=backup->second.DefaultState;}
        else{GameSproutRuntime::AnimatorState state;state.Name="Idle";Layer().States={state};Layer().Transitions.clear();Layer().DefaultState=0;}Layer().SyncedLayerIndex=-1;Layer().SyncedLayerAffectsTiming=false;Layer().MotionOverrides.clear();}
    _Dirty=true;Fill();
}
void AnimatorControllerDialog::RemapSyncIndices(int a,int b){
    for(auto& layer:_Data.Layers){if(layer.SyncedLayerIndex==a)layer.SyncedLayerIndex=b;else if(layer.SyncedLayerIndex==b)layer.SyncedLayerIndex=a;}
    const auto first=_IndependentDrafts.find(a),second=_IndependentDrafts.find(b);GameSproutRuntime::AnimatorLayer x,y;const bool hasX=first!=_IndependentDrafts.end(),hasY=second!=_IndependentDrafts.end();if(hasX)x=first->second;if(hasY)y=second->second;_IndependentDrafts.erase(a);_IndependentDrafts.erase(b);if(hasX)_IndependentDrafts[b]=std::move(x);if(hasY)_IndependentDrafts[a]=std::move(y);
}

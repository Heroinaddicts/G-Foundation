#include "ModelRigDialog.h"
#include "IRuntimeAssets.h"
#include "HumanoidValidation.h"
#include "ui_ModelRigDialog.h"
#include "ui_AnimatorDiscardDialog.h"
#include <QCloseEvent>
#include <QHeaderView>
#include <cmath>
#include <algorithm>

namespace {
void Cell(QTableWidget* table,int row,int column,const QString& text,bool edit=false){
    auto* item=new QTableWidgetItem(text);if(!edit)item->setFlags(item->flags()&~Qt::ItemIsEditable);table->setItem(row,column,item);
}
}
ModelRigDialog::ModelRigDialog(IRuntimeAssets* resources,GameSproutRuntime::ResourceReference reference,QWidget* parent)
    :QDialog(parent),_Ui(new Ui::ModelRigDialogForm),_Resources(resources),_Reference(std::move(reference)){
    _Ui->setupUi(this);_Ui->mappingTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    _Ui->referenceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    connect(_Ui->mappingTable,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){if(!_Filling)SelectBone(row);});
    connect(_Ui->boneCombo,&QComboBox::currentIndexChanged,this,[this]{if(!_Filling){StoreBone();_Dirty=true;}});
    connect(_Ui->referenceTable,&QTableWidget::cellChanged,this,[this]{if(!_Filling)_Dirty=true;});
    connect(_Ui->rigTypeCombo,&QComboBox::currentIndexChanged,this,[this]{if(!_Filling)_Dirty=true;});
    connect(_Ui->humanScaleSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(!_Filling)_Dirty=true;});
    connect(_Ui->suggestButton,&QPushButton::clicked,this,[this]{
        ModelRigImportSettings draft;std::string issue;if(!Read(draft,issue)){Error(issue);return;}_Data=std::move(draft);
        if(!_Resources)return;std::vector<GameSproutRuntime::HumanBoneMapping> suggested;std::string error;
        if(!_Resources->SuggestHumanoidMapping(_Skeleton,suggested,error)){Error(error);return;}
        for(const auto& mapping:suggested)if(std::none_of(_Data.HumanBones.begin(),_Data.HumanBones.end(),[&](const auto& value){return value.Bone==mapping.Bone;}))
            _Data.HumanBones.push_back(mapping);
        _Dirty=true;Fill();Error("建议已填充未映射角色骨。请逐项检查左右、父子关系和参考姿势。");
    });
    connect(_Ui->resetReferenceButton,&QPushButton::clicked,this,[this]{_Data.AnimationType=static_cast<eModelAnimationType>(_Ui->rigTypeCombo->currentIndex());_Data.HumanScale=float(_Ui->humanScaleSpin->value());_Data.ReferencePose.clear();_Dirty=true;Fill();});
    connect(_Ui->saveButton,&QPushButton::clicked,this,[this]{Save();});connect(_Ui->reloadButton,&QPushButton::clicked,this,[this]{Reload();});
    connect(_Ui->closeButton,&QPushButton::clicked,this,&QDialog::reject);Reload();
}
ModelRigDialog::~ModelRigDialog(){delete _Ui;}
void ModelRigDialog::Error(const std::string& text){_Ui->statusLabel->setText(QString::fromStdString(text));}
void ModelRigDialog::Reload(){
    if(_Dirty&&!ConfirmDiscard())return;ModelRigImportSettings data;GameSproutRuntime::SkeletonData skeleton;std::string snapshot,error;
    _Ui->rigTabs->setEnabled(false);_Ui->rigTypeCombo->setEnabled(false);_Ui->saveButton->setEnabled(false);
    if(!_Resources||!_Resources->LoadModelRigSource(_Reference,data,skeleton,snapshot,error)){Error(error.empty()?"Avatar 资源服务不可用。":error);return;}
    _Data=std::move(data);_Skeleton=std::move(skeleton);_Snapshot=std::move(snapshot);_Dirty=false;
    _Ui->rigTabs->setEnabled(true);_Ui->rigTypeCombo->setEnabled(true);_Ui->saveButton->setEnabled(true);Fill();Error({});
}
void ModelRigDialog::Fill(){
    _Filling=true;_Ui->rigTypeCombo->setCurrentIndex(int(_Data.AnimationType));_Ui->humanScaleSpin->setValue(_Data.HumanScale);_Ui->humanScaleSpin->setProperty("loadedHumanScaleValue",_Ui->humanScaleSpin->value());
    _Ui->boneCombo->clear();_Ui->boneCombo->addItem(tr("未映射"),QString());
    for(const auto& node:_Skeleton.Nodes){_Ui->boneCombo->addItem(QString::fromStdString(node.Name),QString::fromStdString(node.Path));_Ui->boneCombo->setItemData(_Ui->boneCombo->count()-1,QString::fromStdString(node.Path),Qt::ToolTipRole);}
    const int count=int(GameSproutRuntime::HumanBone::Count);_Ui->mappingTable->setRowCount(count);
    for(int row=0;row<count;++row){const auto bone=static_cast<GameSproutRuntime::HumanBone>(row);
        Cell(_Ui->mappingTable,row,0,QString::fromUtf8(GameSproutRuntime::HumanBoneName(bone)));
        Cell(_Ui->mappingTable,row,1,GameSproutRuntime::IsRequiredHumanBone(bone)?tr("必需"):tr("可选"));
        const auto found=std::find_if(_Data.HumanBones.begin(),_Data.HumanBones.end(),[&](const auto& value){return value.Bone==bone;});
        const int index=found==_Data.HumanBones.end()?-1:_Ui->boneCombo->findData(QString::fromStdString(found->Path));
        Cell(_Ui->mappingTable,row,2,index>0?_Ui->boneCombo->itemText(index):(found!=_Data.HumanBones.end()?tr("路径已失效"):tr("未映射")));
    }
    _Ui->referenceTable->setRowCount(int(_Skeleton.Nodes.size()));
    for(std::size_t row=0;row<_Skeleton.Nodes.size();++row){const auto& node=_Skeleton.Nodes[row];auto pose=node.Transform;
        for(const auto& value:_Data.ReferencePose)if(value.Path==node.Path){pose=value.Pose;break;}
        Cell(_Ui->referenceTable,int(row),0,QString::fromStdString(node.Name));_Ui->referenceTable->item(int(row),0)->setToolTip(QString::fromStdString(node.Path));
        const float values[]={pose.Position.X,pose.Position.Y,pose.Position.Z,pose.Rotation.X,pose.Rotation.Y,pose.Rotation.Z,pose.Rotation.W,pose.Scale.X,pose.Scale.Y,pose.Scale.Z};
        for(int column=1;column<11;++column)Cell(_Ui->referenceTable,int(row),column,QString::number(values[column-1],'g',9),true);
    }
    _Bone=-1;_Filling=false;_Ui->mappingTable->setCurrentCell(0,0);SelectBone(0);
}
void ModelRigDialog::SelectBone(int row){
    _Bone=row;_Filling=true;const auto found=std::find_if(_Data.HumanBones.begin(),_Data.HumanBones.end(),[&](const auto& value){return int(value.Bone)==row;});
    _Ui->boneCombo->setCurrentIndex(found==_Data.HumanBones.end()?0:_Ui->boneCombo->findData(QString::fromStdString(found->Path)));_Filling=false;
}
void ModelRigDialog::StoreBone(){
    if(_Bone<0)return;const auto bone=static_cast<GameSproutRuntime::HumanBone>(_Bone);const auto path=_Ui->boneCombo->currentData().toString().toStdString();
    auto found=std::find_if(_Data.HumanBones.begin(),_Data.HumanBones.end(),[&](const auto& value){return value.Bone==bone;});
    if(path.empty()){if(found!=_Data.HumanBones.end())_Data.HumanBones.erase(found);}
    else if(found!=_Data.HumanBones.end())found->Path=path;else _Data.HumanBones.push_back({bone,path,{}});
    _Ui->mappingTable->item(_Bone,2)->setText(path.empty()?tr("未映射"):_Ui->boneCombo->currentText());
}
bool ModelRigDialog::Read(ModelRigImportSettings& data,std::string& error){
    data=_Data;data.AnimationType=static_cast<eModelAnimationType>(_Ui->rigTypeCombo->currentIndex());data.HumanScale=_Ui->humanScaleSpin->value()==_Ui->humanScaleSpin->property("loadedHumanScaleValue").toDouble()?_Data.HumanScale:float(_Ui->humanScaleSpin->value());data.ReferencePose.clear();
    for(std::size_t row=0;row<_Skeleton.Nodes.size();++row){float values[10];
        for(int column=1;column<11;++column){bool ok=false;const double value=_Ui->referenceTable->item(int(row),column)->text().toDouble(&ok);
            if(!ok||!std::isfinite(value)||std::fabs(value)>1e20){error="参考姿势必须是有限数值。";return false;}values[column-1]=float(value);}
        GameSproutRuntime::TransformPose pose{{values[0],values[1],values[2]},{values[3],values[4],values[5],values[6]},{values[7],values[8],values[9]}};
        const auto& rest=_Skeleton.Nodes[row].Transform;
        const float original[]={rest.Position.X,rest.Position.Y,rest.Position.Z,rest.Rotation.X,rest.Rotation.Y,rest.Rotation.Z,rest.Rotation.W,rest.Scale.X,rest.Scale.Y,rest.Scale.Z};
        bool changed=false;for(unsigned index=0;index<10;++index)if(values[index]!=original[index])changed=true;
        if(changed)data.ReferencePose.push_back({_Skeleton.Nodes[row].Path,pose});
    }return true;
}
void ModelRigDialog::Save(){ModelRigImportSettings data;std::string error;
    if(!_Resources||!Read(data,error)||!_Resources->SaveModelRigSource(_Reference,data,_Snapshot,error)){Error(error);return;}
    _Dirty=false;Reload();Error("Rig 设置已保存并提交重新导入。");
}
bool ModelRigDialog::ConfirmDiscard(){if(!_Dirty)return true;QDialog dialog(this);Ui::AnimatorDiscardDialogForm form;form.setupUi(&dialog);
    connect(form.cancelButton,&QPushButton::clicked,&dialog,&QDialog::reject);connect(form.discardButton,&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(form.saveButton,&QPushButton::clicked,&dialog,[&]{Save();if(!_Dirty)dialog.accept();else form.errorLabel->setText(_Ui->statusLabel->text());});return dialog.exec()==QDialog::Accepted;
}
void ModelRigDialog::reject(){if(ConfirmDiscard()){_Dirty=false;QDialog::reject();}}
void ModelRigDialog::closeEvent(QCloseEvent* event){if(!ConfirmDiscard()){event->ignore();return;}_Dirty=false;QDialog::closeEvent(event);}

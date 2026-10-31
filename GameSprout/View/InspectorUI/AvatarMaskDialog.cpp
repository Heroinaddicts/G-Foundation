#include "AvatarMaskDialog.h"
#include "IRuntimeAssets.h"
#include "ResourceField.h"
#include "ui_AvatarMaskDialog.h"
#include "ui_AnimatorDiscardDialog.h"
#include <QCloseEvent>
#include <QHeaderView>
#include <QTreeWidget>
#include <QTableWidget>
#include <QStyledItemDelegate>
#include <map>
#include <cmath>

namespace {
class MaskWeightDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        return index.column()==1?QStyledItemDelegate::createEditor(parent,option,index):nullptr;
    }
};
}
AvatarMaskDialog::AvatarMaskDialog(IRuntimeAssets* resources,IAssetsManager* assets,ILibraryManager* library,
    ISelectionManager* selection,GameSproutRuntime::ResourceReference reference,QWidget* parent)
    :QDialog(parent),_Ui(new Ui::AvatarMaskDialogForm),_Resources(resources),_Reference(std::move(reference)) {
    _Ui->setupUi(this);_Ui->skeletonField->Bind(assets,library,selection,eAssetType::Skeleton);
    _Ui->skeletonField->setEnabled(false);
    _Ui->bonesTree->setItemDelegate(new MaskWeightDelegate(_Ui->bonesTree));
    _Ui->bonesTree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    _Ui->bonesTree->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    connect(_Ui->humanPartsTable,&QTableWidget::cellChanged,this,[this](int,int){if(!_Filling){_Dirty=true;if(StoreTree())Error({});}});
    connect(_Ui->bonesTree,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){if(!_Filling)ItemChanged(item,column);});
    connect(_Ui->defaultWeightSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this](double value){
        if(_Filling)return;if(!StoreTree())return;_Data.DefaultWeight=float(value);_Dirty=true;FillTree();
    });
    connect(_Ui->rootMotionWeightSpin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{if(!_Filling)_Dirty=true;});
    connect(_Ui->allButton,&QPushButton::clicked,this,[this]{_Data.Nodes.clear();_Data.HumanParts.clear();_Data.DefaultWeight=1;_Dirty=true;FillTree();});
    connect(_Ui->noneButton,&QPushButton::clicked,this,[this]{_Data.Nodes.clear();_Data.HumanParts.clear();_Data.DefaultWeight=0;_Dirty=true;FillTree();});
    connect(_Ui->reloadButton,&QPushButton::clicked,this,[this]{Reload();});
    connect(_Ui->saveButton,&QPushButton::clicked,this,[this]{Save();});
    connect(_Ui->closeButton,&QPushButton::clicked,this,&QDialog::reject);Reload();
}
AvatarMaskDialog::~AvatarMaskDialog(){delete _Ui;}
void AvatarMaskDialog::Error(const std::string& value){_Ui->statusLabel->setText(QString::fromStdString(value));}
void AvatarMaskDialog::Reload(){
    if(_Dirty&&!ConfirmDiscard())return;std::string error;GameSproutRuntime::AvatarMaskData data;GameSproutRuntime::SkeletonData skeleton;
    auto* provider=_Resources?_Resources->GetResourceProvider():nullptr;
    if(!_Resources||!_Resources->LoadAvatarMaskSource(_Reference,data,_Snapshot,error)||(data.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&(!provider||!provider->LoadSkeleton(data.Skeleton,skeleton,error)))){
        Error(error.empty()?"无法读取 Mask 源文件或骨架。":error);_Ui->saveButton->setEnabled(false);return;
    }
    _Data=std::move(data);_Skeleton=std::move(skeleton);_Dirty=false;_Ui->saveButton->setEnabled(true);
    _Filling=true;_Ui->skeletonField->SetValue(_Data.Skeleton);_Ui->rootMotionWeightSpin->setValue(_Data.RootMotionWeight);_Filling=false;
    Error({});FillTree();
}
void AvatarMaskDialog::FillTree(){
    const bool human=_Data.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid;
    _Ui->bonesTree->setVisible(!human);_Ui->humanPartsTable->setVisible(human);_Ui->humanMaskHelpLabel->setVisible(human);_Ui->rootHelpLabel->setVisible(!human);
    _Ui->skeletonField->setVisible(!human);_Ui->skeletonLabel->setVisible(!human);_Ui->descriptionLabel->setVisible(!human);
    _Ui->defaultWeightLabel->setText(human?tr("默认部位权重"):tr("默认骨骼权重"));
    _Ui->maskModeLabel->setText(human?tr("Humanoid · 人体部位"):tr("Transform · 绑定骨架路径"));
    if(human){_Filling=true;_Ui->defaultWeightSpin->setValue(_Data.DefaultWeight);_Ui->humanPartsTable->setRowCount(int(GameSproutRuntime::HumanBodyPart::Count));
        for(int row=0;row<int(GameSproutRuntime::HumanBodyPart::Count);++row){const auto part=static_cast<GameSproutRuntime::HumanBodyPart>(row);float weight=_Data.DefaultWeight;for(const auto& value:_Data.HumanParts)if(value.Part==part)weight=value.Weight;
            auto* label=new QTableWidgetItem(QString::fromUtf8(GameSproutRuntime::HumanBodyPartName(part)));label->setFlags(label->flags()&~Qt::ItemIsEditable);_Ui->humanPartsTable->setItem(row,0,label);_Ui->humanPartsTable->setItem(row,1,new QTableWidgetItem(QString::number(weight,'g',9)));}
        _Filling=false;return;
    }
    _Filling=true;_Ui->defaultWeightSpin->setValue(_Data.DefaultWeight);_Ui->bonesTree->clear();_Items.assign(_Skeleton.Nodes.size(),nullptr);
    std::map<std::string,float> overrides;for(const auto& node:_Data.Nodes)overrides.emplace(node.Path,node.Weight);
    std::vector<float> weights(_Skeleton.Nodes.size(),_Data.DefaultWeight);
    std::vector<bool> known(_Skeleton.Nodes.size(),false);
    // Skeleton records may be stored in any order. Resolve parent chains without recursion.
    for(std::size_t index=0;index<_Skeleton.Nodes.size();++index){
        std::vector<std::size_t> chain;std::int32_t current=std::int32_t(index);
        while(current>=0&&!known[std::size_t(current)]){
            if(std::size_t(current)>=_Skeleton.Nodes.size()||chain.size()>=_Skeleton.Nodes.size()){
                Error("骨架层级无效。");_Ui->saveButton->setEnabled(false);_Filling=false;return;
            }
            chain.push_back(std::size_t(current));current=_Skeleton.Nodes[std::size_t(current)].Parent;
            if(current>=std::int32_t(_Skeleton.Nodes.size())){Error("骨架父节点无效。");_Ui->saveButton->setEnabled(false);_Filling=false;return;}
        }
        while(!chain.empty()){const auto nodeIndex=chain.back();chain.pop_back();const auto& node=_Skeleton.Nodes[nodeIndex];
            const auto found=overrides.find(node.Path);weights[nodeIndex]=found!=overrides.end()?found->second:(node.Parent>=0?weights[std::size_t(node.Parent)]:_Data.DefaultWeight);known[nodeIndex]=true;
        }
    }
    for(std::size_t index=0;index<_Skeleton.Nodes.size();++index){
        const auto& node=_Skeleton.Nodes[index];auto* item=new QTreeWidgetItem;
        _Items[index]=item;
        item->setText(0,QString::fromStdString(node.Name));item->setToolTip(0,QString::fromStdString(node.Path));
        item->setFlags(item->flags()|Qt::ItemIsUserCheckable|Qt::ItemIsEditable);item->setCheckState(0,weights[index]>0?Qt::Checked:Qt::Unchecked);
        item->setText(1,QString::number(weights[index],'g',9));item->setData(0,Qt::UserRole,qulonglong(index));
    }
    for(std::size_t index=0;index<_Skeleton.Nodes.size();++index){const auto parent=_Skeleton.Nodes[index].Parent;
        if(parent>=0)_Items[std::size_t(parent)]->addChild(_Items[index]);else _Ui->bonesTree->addTopLevelItem(_Items[index]);}
    _Ui->bonesTree->expandToDepth(2);_Filling=false;
}
void AvatarMaskDialog::ItemChanged(QTreeWidgetItem* item,int column){
    if(!item)return;bool ok=true;double value=column==0?(item->checkState(0)==Qt::Checked?1.0:0.0):item->text(1).toDouble(&ok);
    if(!ok||!std::isfinite(value)||value<0||value>1){Error("骨骼权重必须是 0 到 1 的数字。");_Dirty=true;return;}
    _Filling=true;
    const auto apply=[&](auto&& self,QTreeWidgetItem* current)->void{
        // Names and paths belong to the skeleton. The editable value is only the mask weight.
        const auto index=current->data(0,Qt::UserRole).toULongLong();current->setText(0,QString::fromStdString(_Skeleton.Nodes[index].Name));
        current->setCheckState(0,value>0?Qt::Checked:Qt::Unchecked);current->setText(1,QString::number(value,'g',9));
        for(int child=0;child<current->childCount();++child)self(self,current->child(child));
    };apply(apply,item);_Filling=false;_Dirty=true;StoreTree();Error({});
}
bool AvatarMaskDialog::StoreTree(){
    if(_Data.Mode==GameSproutRuntime::AvatarMaskMode::Humanoid){std::vector<GameSproutRuntime::HumanBodyPartWeight> parts;
        for(int row=0;row<_Ui->humanPartsTable->rowCount();++row){bool ok=false;const double value=_Ui->humanPartsTable->item(row,1)->text().toDouble(&ok);if(!ok||!std::isfinite(value)||value<0||value>1){Error("人体部位权重必须是 0 到 1 的数字。");return false;}
            if(float(value)!=_Data.DefaultWeight)parts.push_back({static_cast<GameSproutRuntime::HumanBodyPart>(row),float(value)});}
        _Data.HumanParts=std::move(parts);return true;
    }
    std::vector<float> weights(_Items.size());
    for(std::size_t index=0;index<_Items.size();++index){bool ok=false;const double value=_Items[index]->text(1).toDouble(&ok);
        if(!ok||!std::isfinite(value)||value<0||value>1){Error("骨骼权重必须是 0 到 1 的数字。");return false;}weights[index]=float(value);}
    std::vector<GameSproutRuntime::AvatarMaskNode> nodes;
    for(std::size_t index=0;index<_Items.size();++index){const auto parent=_Skeleton.Nodes[index].Parent;const float inherited=parent>=0?weights[std::size_t(parent)]:_Data.DefaultWeight;
        if(weights[index]!=inherited)nodes.push_back({_Skeleton.Nodes[index].Path,weights[index]});}
    _Data.Nodes=std::move(nodes);return true;
}
void AvatarMaskDialog::Save(){
    if(!_Resources||!StoreTree())return;std::string error;_Data.RootMotionWeight=float(_Ui->rootMotionWeightSpin->value());
    if(!_Resources->SaveAvatarMaskSource(_Reference,_Data,_Snapshot,error)){Error(error);_Dirty=true;return;}
    _Dirty=false;Reload();Error("已保存并提交资源导入。");
}
bool AvatarMaskDialog::ConfirmDiscard(){
    if(!_Dirty)return true;QDialog dialog(this);Ui::AnimatorDiscardDialogForm form;form.setupUi(&dialog);
    connect(form.cancelButton,&QPushButton::clicked,&dialog,&QDialog::reject);connect(form.discardButton,&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(form.saveButton,&QPushButton::clicked,&dialog,[this,&dialog,&form]{Save();if(!_Dirty)dialog.accept();else form.errorLabel->setText(_Ui->statusLabel->text());});
    return dialog.exec()==QDialog::Accepted;
}
void AvatarMaskDialog::reject(){if(!ConfirmDiscard())return;_Dirty=false;QDialog::reject();}
void AvatarMaskDialog::closeEvent(QCloseEvent* event){if(!ConfirmDiscard()){event->ignore();return;}_Dirty=false;QDialog::closeEvent(event);}

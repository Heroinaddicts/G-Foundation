#include "ScriptCompositeField.h"
#include "ScriptPropertyRow.h"
#include "ui_ScriptCompositeField.h"
#include "ui_ScriptContainerItem.h"
#include <QSignalBlocker>

ScriptCompositeField::ScriptCompositeField(const ScriptFieldSnapshot& snapshot,const ScriptFieldTarget& target,
    const ComponentEditContext& context,const ScriptFieldPath& path,const std::string& rootID,QWidget* parent)
    :QWidget(parent),_Ui(new Ui::ScriptCompositeFieldForm),_Target(target),_Context(context),_Path(path),_RootField(rootID){
    _Ui->setupUi(this);
    setProperty("scriptFieldID",QString::fromStdString(snapshot.Info.ID));
    QObject::connect(_Ui->foldButton,&QToolButton::toggled,this,[this](bool expanded){_Ui->body->setVisible(expanded);_Ui->foldButton->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);});
    QObject::connect(_Ui->addButton,&QToolButton::clicked,this,[this]{const auto size=_Snapshot.Value.Kind==GameSproutRuntime::ScriptValueKind::Sequence?_Snapshot.Value.Elements.size():_Snapshot.Value.Entries.size();if(ContainerChanged)ContainerChanged(_Path,{ScriptContainerAction::Insert,size});});
    QObject::connect(_Ui->sizeSpin,&QSpinBox::editingFinished,this,[this]{const auto size=std::size_t(_Ui->sizeSpin->value());if(size!=_Snapshot.Value.Elements.size()&&ContainerChanged)ContainerChanged(_Path,{ScriptContainerAction::Resize,size});});
    Rebuild(snapshot);Load(snapshot);
}
ScriptCompositeField::~ScriptCompositeField(){Changed={};ContainerChanged={};Invalid={};delete _Ui;}
void ScriptCompositeField::Load(const ScriptFieldSnapshot& snapshot){
    bool same=_Snapshot.Children.size()==snapshot.Children.size()&&_Rows.size()==snapshot.Children.size();
    if(same)for(std::size_t index=0;index<_Rows.size();++index)if(!ScriptPropertyRow::SameDefinition(_Rows[index]->Definition(),snapshot.Children[index].Info)){same=false;break;}
    if(!same)Rebuild(snapshot);
    _Snapshot=snapshot;
    using GameSproutRuntime::ScriptValueKind;
    const bool sequence=snapshot.Value.Kind==ScriptValueKind::Sequence,dictionary=snapshot.Value.Kind==ScriptValueKind::Dictionary;
    const bool container=sequence||dictionary;
    const QString name=QString::fromStdString(snapshot.Info.Name.empty()?snapshot.Info.ID:snapshot.Info.Name);
    _Ui->foldButton->setText(name);_Ui->foldButton->setToolTip(QString::fromStdString(snapshot.Info.TypeName));
    _Ui->sizeLabel->setVisible(container);_Ui->sizeSpin->setVisible(container);
    {const QSignalBlocker block(_Ui->sizeSpin);_Ui->sizeSpin->setValue(int(sequence?snapshot.Value.Elements.size():snapshot.Value.Entries.size()));}
    _Ui->sizeSpin->setEnabled(sequence&&snapshot.Info.FixedLength<0);
    _Ui->addButton->setVisible(container&&(!sequence||snapshot.Info.FixedLength<0));
    for(std::size_t index=0;index<_Rows.size();++index)_Rows[index]->Load(snapshot.Children[index]);
}
void ScriptCompositeField::Rebuild(const ScriptFieldSnapshot& snapshot){
    std::map<std::string,bool> expansion;for(const auto* row:_Rows)row->SaveExpansion(expansion);
    _Rows.clear();for(auto* host:_Hosts)delete host;_Hosts.clear();
    using GameSproutRuntime::ScriptValueKind;
    const bool structure=snapshot.Value.Kind==ScriptValueKind::Structure,dictionary=snapshot.Value.Kind==ScriptValueKind::Dictionary;
    const std::size_t groups=dictionary?snapshot.Children.size()/2:snapshot.Children.size();
    for(std::size_t index=0;index<groups;++index){
        auto* host=new QWidget(_Ui->body);Ui::ScriptContainerItemForm item;item.setupUi(host);
        _Ui->childrenLayout->addWidget(host);_Hosts.push_back(host);
        item.header->setVisible(!structure);item.indexLabel->setText(dictionary?tr("Entry %1").arg(index):tr("Element %1").arg(index));
        item.removeButton->setVisible(dictionary||snapshot.Info.FixedLength<0);
        item.upButton->setVisible(!dictionary);item.downButton->setVisible(!dictionary);
        item.upButton->setEnabled(index>0);item.downButton->setEnabled(index+1<groups);
        const auto command=[this,index](ScriptContainerAction action,std::size_t destination=0){if(ContainerChanged)ContainerChanged(_Path,{action,index,destination});};
        QObject::connect(item.removeButton,&QToolButton::clicked,host,[command]{command(ScriptContainerAction::Remove);});
        QObject::connect(item.upButton,&QToolButton::clicked,host,[command,index]{command(ScriptContainerAction::Move,index-1);});
        QObject::connect(item.downButton,&QToolButton::clicked,host,[command,index]{command(ScriptContainerAction::Move,index+1);});
        for(std::size_t column=0;column<(dictionary?2:1);++column){
            const auto& child=snapshot.Children[dictionary?index*2+column:index];auto path=_Path;
            if(structure)path.push_back({ScriptPathKind::Member,child.Info.ID});
            else path.push_back({dictionary?(column?ScriptPathKind::EntryValue:ScriptPathKind::EntryKey):ScriptPathKind::Element,{},index});
            auto* row=new ScriptPropertyRow(child,_Target,_Context,host,path,_RootField);item.fieldsLayout->addWidget(row);_Rows.push_back(row);
            if(!structure&&!dictionary){item.indexLabel->hide();item.layout->removeWidget(item.header);row->AttachItemControls(item.header);}
            row->PathChanged=[this](const auto& path,const auto& value){const auto callback=Changed;if(callback)callback(path,value);};
            row->ContainerChanged=[this](const auto& path,const auto& edit){const auto callback=ContainerChanged;if(callback)callback(path,edit);};
            row->Invalid=[this](const auto& message){if(Invalid)Invalid(message);};
            row->RestoreExpansion(expansion);
        }
    }
    _Snapshot=snapshot;
}

void ScriptCompositeField::AttachItemControls(QWidget* controls){
    controls->setParent(this);_Ui->headerLayout->addWidget(controls);
}

namespace {
std::string ExpansionKey(const std::string& root,const ScriptFieldPath& path){
    std::string key=root;
    for(const auto& step:path)key+="/"+std::to_string(unsigned(step.Kind))+":"+std::to_string(step.Member.size())+":"+step.Member+":"+std::to_string(step.Index);
    return key;
}
}
void ScriptCompositeField::SaveExpansion(std::map<std::string,bool>& values)const{
    values[ExpansionKey(_RootField,_Path)]=_Ui->foldButton->isChecked();
    for(const auto* row:_Rows)row->SaveExpansion(values);
}
void ScriptCompositeField::RestoreExpansion(const std::map<std::string,bool>& values){
    const auto found=values.find(ExpansionKey(_RootField,_Path));if(found!=values.end())_Ui->foldButton->setChecked(found->second);
    for(auto* row:_Rows)row->RestoreExpansion(values);
}

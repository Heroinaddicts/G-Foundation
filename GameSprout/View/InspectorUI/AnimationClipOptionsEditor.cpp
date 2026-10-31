#include "AnimationClipOptionsEditor.h"
#include "ui_AnimationClipOptionsEditor.h"
#include <QHeaderView>
#include <QSignalBlocker>
#include <QLocale>
#include <cmath>
#include <limits>
#include <algorithm>
#include <array>

AnimationClipOptionsEditor::AnimationClipOptionsEditor(QWidget* parent):QWidget(parent),_Ui(new Ui::AnimationClipOptionsEditorForm()) {
    _Ui->setupUi(this);
    _Ui->animationEventsTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    _Ui->animationEventsTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    _Ui->animationEventsTable->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    QObject::connect(_Ui->extractRootMotionCheck,&QCheckBox::toggled,this,[this](bool enabled){_Ui->rootMotionNodeCombo->setEnabled(enabled);NotifyChanged();});
    QObject::connect(_Ui->rootMotionNodeCombo,&QComboBox::currentTextChanged,this,[this]{NotifyChanged();});
    QObject::connect(_Ui->animationEventsTable,&QTableWidget::itemChanged,this,[this]{NotifyChanged();});
    QObject::connect(_Ui->addAnimationEventButton,&QPushButton::clicked,this,[this]{
        _Loading=true;const int row=_Ui->animationEventsTable->rowCount();_Ui->animationEventsTable->insertRow(row);
        const auto* previous=row?_Ui->animationEventsTable->item(row-1,0):nullptr;
        _Ui->animationEventsTable->setItem(row,0,new QTableWidgetItem(previous?previous->text():QStringLiteral("0")));
        _Ui->animationEventsTable->setItem(row,1,new QTableWidgetItem("Event"));
        _Ui->animationEventsTable->setItem(row,2,new QTableWidgetItem());
        _Ui->animationEventsTable->setCurrentCell(row,1);_Loading=false;NotifyChanged();
    });
    QObject::connect(_Ui->sortAnimationEventsButton,&QPushButton::clicked,this,[this]{
        struct Row {double Time;std::array<QString,3> Text;};std::vector<Row> rows;
        for(int row=0;row<_Ui->animationEventsTable->rowCount();++row) {
            Row value;
            for(int column=0;column<3;++column){const auto* item=_Ui->animationEventsTable->item(row,column);value.Text[column]=item?item->text():QString();}
            bool valid=false;value.Time=QLocale::c().toDouble(value.Text[0],&valid);
            if(!valid||!std::isfinite(value.Time)||value.Time<0){_Ui->optionsErrorLabel->setText(tr("请先修正无效的事件时间。"));return;}
            rows.push_back(std::move(value));
        }
        std::stable_sort(rows.begin(),rows.end(),[](const Row& a,const Row& b){return a.Time<b.Time;});
        _Loading=true;
        for(int row=0;row<int(rows.size());++row)for(int column=0;column<3;++column)_Ui->animationEventsTable->setItem(row,column,new QTableWidgetItem(rows[row].Text[column]));
        _Loading=false;NotifyChanged();
    });
    QObject::connect(_Ui->removeAnimationEventButton,&QPushButton::clicked,this,[this]{
        const int row=_Ui->animationEventsTable->currentRow();if(row<0)return;
        _Loading=true;_Ui->animationEventsTable->removeRow(row);_Loading=false;NotifyChanged();
    });
}
AnimationClipOptionsEditor::~AnimationClipOptionsEditor(){delete _Ui;}
void AnimationClipOptionsEditor::SetNodePaths(const std::vector<std::string>& paths) {
    const QString selected=_Ui->rootMotionNodeCombo->currentText();
    const QSignalBlocker blocker(_Ui->rootMotionNodeCombo);_Ui->rootMotionNodeCombo->clear();
    for(const auto& path:paths)_Ui->rootMotionNodeCombo->addItem(QString::fromStdString(path));
    _Ui->rootMotionNodeCombo->setCurrentText(selected);
}
void AnimationClipOptionsEditor::Load(const Json::Value& options) {
    _Loading=true;_Original=options.isObject()?options:Json::Value(Json::objectValue);
    _Ui->extractRootMotionCheck->setChecked(_Original.get("ExtractRootMotion",false).asBool());
    _Ui->rootMotionNodeCombo->setCurrentText(QString::fromStdString(_Original.get("RootMotionNode","").asString()));
    _Ui->rootMotionNodeCombo->setEnabled(_Ui->extractRootMotionCheck->isChecked());
    _Ui->animationEventsTable->setRowCount(0);
    const auto events=_Original.get("Events",Json::Value(Json::arrayValue));
    for(const auto& event:events) {
        const int row=_Ui->animationEventsTable->rowCount();_Ui->animationEventsTable->insertRow(row);
        _Ui->animationEventsTable->setItem(row,0,new QTableWidgetItem(QString::number(event["Time"].asDouble(),'g',12)));
        _Ui->animationEventsTable->setItem(row,1,new QTableWidgetItem(QString::fromStdString(event["Name"].asString())));
        _Ui->animationEventsTable->setItem(row,2,new QTableWidgetItem(QString::fromStdString(event.get("Payload","").asString())));
    }
    _Ui->optionsErrorLabel->clear();_Touched=false;_Loading=false;
}
bool AnimationClipOptionsEditor::Read(Json::Value& options,std::string& error) const {
    error.clear();options=_Original;
    if(!_Touched)return true;
    options["ExtractRootMotion"]=_Ui->extractRootMotionCheck->isChecked();
    options["RootMotionNode"]=_Ui->rootMotionNodeCombo->currentText().toStdString();
    options["Events"]=Json::Value(Json::arrayValue);
    for(int row=0;row<_Ui->animationEventsTable->rowCount();++row) {
        const auto text=[&](int column){const auto* item=_Ui->animationEventsTable->item(row,column);return item?item->text():QString();};
        bool valid=false;const double time=QLocale::c().toDouble(text(0),&valid);
        if(!valid||!std::isfinite(time)||time<0||time>std::numeric_limits<float>::max()){error="事件时间必须是非负的有限秒数。";return false;}
        Json::Value event(Json::objectValue);event["Time"]=time;event["Name"]=text(1).toStdString();event["Payload"]=text(2).toStdString();options["Events"].append(std::move(event));
    }
    return true;
}
void AnimationClipOptionsEditor::NotifyChanged() {
    if(_Loading)return;
    _Touched=true;
    Json::Value value;std::string error;Read(value,error);
    _Ui->optionsErrorLabel->setText(QString::fromStdString(error));
    if(Changed)Changed();
}

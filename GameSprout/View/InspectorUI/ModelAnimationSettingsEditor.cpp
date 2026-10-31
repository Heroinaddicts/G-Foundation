#include "ModelAnimationSettingsEditor.h"
#include "AnimationClipOptionsEditor.h"
#include "ui_ModelAnimationSettingsEditor.h"
#include <QSignalBlocker>

ModelAnimationSettingsEditor::ModelAnimationSettingsEditor(QWidget* parent):QWidget(parent),_Ui(new Ui::ModelAnimationSettingsEditorForm()) {
    _Ui->setupUi(this);
    QObject::connect(_Ui->configuredClipsList,&QListWidget::currentRowChanged,this,[this](int row){SelectClip(row);});
    _Ui->clipOptionsEditor->Changed=[this]{if(!_Loading&&Changed)Changed();};
    QObject::connect(_Ui->addClipOptionsButton,&QPushButton::clicked,this,[this]{
        const auto name=_Ui->sourceClipCombo->currentText().toStdString();
        if(name.empty()){_Ui->modelOptionsErrorLabel->setText(tr("请先选择或输入源动画片段名称。"));return;}
        if(!StoreCurrent())return;
        for(Json::ArrayIndex i=0;i<_Clips.size();++i)if(_Clips[i]["Name"]==name){_Ui->configuredClipsList->setCurrentRow(int(i));return;}
        Json::Value options(Json::objectValue);options["Name"]=name;options["ExtractRootMotion"]=false;options["RootMotionNode"]="";options["Events"]=Json::Value(Json::arrayValue);
        _Clips.append(std::move(options));Rebuild(int(_Clips.size()-1));if(Changed)Changed();
    });
    QObject::connect(_Ui->removeClipOptionsButton,&QPushButton::clicked,this,[this]{
        if(_Row<0||_Row>=int(_Clips.size()))return;
        Json::Value removed;_Clips.removeIndex(Json::ArrayIndex(_Row),&removed);
        Rebuild(std::min(_Row,int(_Clips.size())-1));if(Changed)Changed();
    });
    Rebuild(-1);
}
ModelAnimationSettingsEditor::~ModelAnimationSettingsEditor(){_Ui->clipOptionsEditor->Changed={};delete _Ui;}
void ModelAnimationSettingsEditor::SetSources(const std::vector<std::string>& clips,const std::vector<std::string>& nodes) {
    const QString selected=_Ui->sourceClipCombo->currentText();
    const QSignalBlocker blocker(_Ui->sourceClipCombo);_Ui->sourceClipCombo->clear();
    for(const auto& clip:clips)_Ui->sourceClipCombo->addItem(QString::fromStdString(clip));
    if(!selected.isEmpty())_Ui->sourceClipCombo->setCurrentText(selected);
    _Ui->clipOptionsEditor->SetNodePaths(nodes);
}
void ModelAnimationSettingsEditor::Load(const Json::Value& clips) {
    _Clips=clips.isArray()?clips:Json::Value(Json::arrayValue);Rebuild(_Clips.empty()?-1:0);
}
void ModelAnimationSettingsEditor::Rebuild(int row) {
    _Loading=true;_Row=-1;_Ui->configuredClipsList->clear();
    for(const auto& clip:_Clips)_Ui->configuredClipsList->addItem(QString::fromStdString(clip["Name"].asString()));
    _Ui->configuredClipsList->setCurrentRow(row);_Loading=false;
    SelectClip(row);_Ui->modelOptionsErrorLabel->clear();
}
bool ModelAnimationSettingsEditor::StoreCurrent() {
    if(_Row<0||_Row>=int(_Clips.size()))return true;
    Json::Value value;std::string error;
    if(!_Ui->clipOptionsEditor->Read(value,error)){_Ui->modelOptionsErrorLabel->setText(QString::fromStdString(error));return false;}
    value["Name"]=_Clips[Json::ArrayIndex(_Row)]["Name"];
    _Clips[Json::ArrayIndex(_Row)]=std::move(value);return true;
}
void ModelAnimationSettingsEditor::SelectClip(int row) {
    if(_Loading)return;
    if(!StoreCurrent()) {const QSignalBlocker blocker(_Ui->configuredClipsList);_Ui->configuredClipsList->setCurrentRow(_Row);return;}
    _Row=row;const bool valid=row>=0&&row<int(_Clips.size());
    _Ui->clipOptionsEditor->setEnabled(valid);_Ui->removeClipOptionsButton->setEnabled(valid);
    _Ui->clipOptionsEditor->Load(valid?_Clips[Json::ArrayIndex(row)]:Json::Value(Json::objectValue));
    if(valid){const QSignalBlocker blocker(_Ui->sourceClipCombo);_Ui->sourceClipCombo->setCurrentText(QString::fromStdString(_Clips[Json::ArrayIndex(row)]["Name"].asString()));}
}
bool ModelAnimationSettingsEditor::Read(Json::Value& clips,std::string& error) const {
    clips=_Clips;error.clear();
    if(_Row>=0&&_Row<int(clips.size())) {
        Json::Value value;if(!_Ui->clipOptionsEditor->Read(value,error))return false;
        value["Name"]=clips[Json::ArrayIndex(_Row)]["Name"];clips[Json::ArrayIndex(_Row)]=std::move(value);
    }
    return true;
}

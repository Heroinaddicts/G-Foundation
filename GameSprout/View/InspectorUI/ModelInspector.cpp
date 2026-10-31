#include "ModelInspector.h"
#include "ui_ModelInspector.h"
#include "ModelAnimationSettingsEditor.h"

ModelInspector::ModelInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::ModelInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
        connect(_Ui->configureRigButton,&QPushButton::clicked,this,[this]{if(ConfigureRig)ConfigureRig();});
        _Ui->animationOptionsEditor->Changed=[this]{if(Changed)Changed();};
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
ModelInspector::~ModelInspector() { _Ui->animationOptionsEditor->Changed={};delete _Ui; }
void ModelInspector::Load(const Json::Value& settings) {
    ImportSettingsInspector::Load(settings);_Ui->animationOptionsEditor->Load(settings["AnimationClips"]);
}
bool ModelInspector::Read(Json::Value& settings,std::string& error) const {
    if(!ImportSettingsInspector::Read(settings,error))return false;
    Json::Value clips;if(!_Ui->animationOptionsEditor->Read(clips,error))return false;
    if(settings.isMember("AnimationClips")||!clips.empty())settings["AnimationClips"]=std::move(clips);
    return true;
}
void ModelInspector::SetAnimationSources(const std::vector<std::string>& clips,const std::vector<std::string>& nodes) {
    _Ui->animationOptionsEditor->SetSources(clips,nodes);
}
void ModelInspector::RefreshEnabled() {
    _Ui->normalSmoothingAngleSpin->setEnabled(_Ui->normalsModeCombo->currentData()=="Calculate");
    _Ui->animationTypeCombo->setEnabled(_Ui->importAnimationCheck->isChecked());
    _Ui->animationOptionsEditor->setEnabled(_Ui->importAnimationCheck->isChecked());
}
bool ModelInspector::Validate(const Json::Value & settings, std::string & error) const {
    if (settings["GenerateLightmapUVs"].asBool()) { error="自动光照 UV 尚未实现，请关闭此项。";return false; }
    return true;
}

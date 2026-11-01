#include "ModelInspector.h"
#include "ui_ModelInspector.h"
#include "IAnimationUI.h"
#include "IModelAnimationEditor.h"

ModelInspector::ModelInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::ModelInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
        connect(_Ui->configureRigButton,&QPushButton::clicked,this,[this]{if(ConfigureRig)ConfigureRig();});

    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
ModelInspector::~ModelInspector() { if (_AnimationEditor) _AnimationEditor->Changed={};delete _Ui; }
void ModelInspector::BindAnimationUI(IAnimationUI* animations) {
    if (_AnimationEditor || !animations) return;
    _AnimationEditor=animations->CreateModelAnimationEditor(_Ui->animationOptionsHost);
    if (_AnimationEditor) {
        _Ui->animationOptionsLayout->addWidget(_AnimationEditor);
        _AnimationEditor->Changed=[this]{if(Changed)Changed();};
    }
}
void ModelInspector::Load(const Json::Value& settings) {
    ImportSettingsInspector::Load(settings);if (_AnimationEditor) _AnimationEditor->Load(settings["AnimationClips"]);
}
bool ModelInspector::Read(Json::Value& settings,std::string& error) const {
    if(!ImportSettingsInspector::Read(settings,error))return false;
    if (!_AnimationEditor) return true;
    Json::Value clips;if(!_AnimationEditor->Read(clips,error))return false;
    if(settings.isMember("AnimationClips")||!clips.empty())settings["AnimationClips"]=std::move(clips);
    return true;
}
void ModelInspector::SetAnimationSources(const std::vector<std::string>& clips,const std::vector<std::string>& nodes) {
    if (_AnimationEditor) _AnimationEditor->SetSources(clips,nodes);
}
void ModelInspector::RefreshEnabled() {
    _Ui->normalSmoothingAngleSpin->setEnabled(_Ui->normalsModeCombo->currentData()=="Calculate");
    _Ui->animationTypeCombo->setEnabled(_Ui->importAnimationCheck->isChecked());
    if (_AnimationEditor) _AnimationEditor->setEnabled(_Ui->importAnimationCheck->isChecked());
}
bool ModelInspector::Validate(const Json::Value & settings, std::string & error) const {
    if (settings["GenerateLightmapUVs"].asBool()) { error="自动光照 UV 尚未实现，请关闭此项。";return false; }
    return true;
}

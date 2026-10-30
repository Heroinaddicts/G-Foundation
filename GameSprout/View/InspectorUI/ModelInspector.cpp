#include "ModelInspector.h"
#include "ui_ModelInspector.h"

ModelInspector::ModelInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::ModelInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
ModelInspector::~ModelInspector() { delete _Ui; }
void ModelInspector::RefreshEnabled() {
    _Ui->normalSmoothingAngleSpin->setEnabled(_Ui->normalsModeCombo->currentData()=="Calculate");
    _Ui->animationTypeCombo->setEnabled(_Ui->importAnimationCheck->isChecked());
}
bool ModelInspector::Validate(const Json::Value & settings, std::string & error) const {
    if (settings["GenerateLightmapUVs"].asBool()) { error="自动光照 UV 尚未实现，请关闭此项。";return false; }
    return true;
}

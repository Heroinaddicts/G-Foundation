#include "AudioInspector.h"
#include "ui_AudioInspector.h"

AudioInspector::AudioInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::AudioInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
AudioInspector::~AudioInspector() { delete _Ui; }
void AudioInspector::RefreshEnabled() {
    _Ui->normalizeCheck->setEnabled(_Ui->forceToMonoCheck->isChecked());
    _Ui->qualitySpin->setEnabled(_Ui->compressionFormatCombo->currentData()=="Vorbis");
    _Ui->sampleRateOverrideEdit->setEnabled(_Ui->sampleRateModeCombo->currentData()=="Override");
}
bool AudioInspector::Validate(const Json::Value & settings, std::string & error) const {
    if (settings["ForceToMono"].asBool() && settings["Ambisonic"].asBool()) {
        error="Ambisonic 音频不能同时转为单声道。";return false;
    }
    return true;
}

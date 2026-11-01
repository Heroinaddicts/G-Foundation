#include "VideoInspector.h"
#include "ui_VideoInspector.h"
#include <limits>

VideoInspector::VideoInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::VideoInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
VideoInspector::~VideoInspector() { delete _Ui; }
void VideoInspector::RefreshEnabled() {
    const bool transcode=_Ui->transcodeCheck->isChecked();
    _Ui->codecCombo->setEnabled(transcode);
    _Ui->maxWidthEdit->setEnabled(transcode);
    _Ui->maxHeightEdit->setEnabled(transcode);
}
bool VideoInspector::Validate(const Json::Value & settings, std::string & error) const {
    if (settings["MaxWidth"].asUInt()>static_cast<uint32_t>(std::numeric_limits<int>::max())
        || settings["MaxHeight"].asUInt()>static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        error="视频尺寸不能超过当前转码器的 int32 范围。";return false;
    }
    const bool pixels=settings["EncodeAlpha"].asBool() || settings["FlipHorizontal"].asBool() || settings["FlipVertical"].asBool();
    if (!settings["Transcode"].asBool() && pixels) { error="透明通道和翻转需要启用重新编码。";return false; }
    const std::string codec=settings["Codec"].asString();
    if (settings["EncodeAlpha"].asBool() && codec!="Auto" && codec!="VP8") {
        error="保留透明通道时请选择 Auto 或 VP8。";return false;
    }
    return true;
}

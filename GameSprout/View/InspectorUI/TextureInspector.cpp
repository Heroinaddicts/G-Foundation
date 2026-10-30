#include "TextureInspector.h"
#include "ui_TextureInspector.h"
#include <QComboBox>
#include <QCheckBox>

TextureInspector::TextureInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::TextureInspectorForm()) {
    _Ui->setupUi(this);
    for (QComboBox * combo : {_Ui->colorSpaceCombo, _Ui->alphaSourceCombo, _Ui->filterCombo, _Ui->wrapUCombo, _Ui->wrapVCombo}) {
        const QStringList values = combo->property("values").toStringList();
        Q_ASSERT(values.size() == combo->count());
        for (int i = 0; i < combo->count(); ++i) combo->setItemData(i, values.value(i));
        QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { if (!_Loading && Changed) Changed(); });
    }
    QObject::connect(_Ui->maxSizeCombo, &QComboBox::currentTextChanged, this, [this] { if (!_Loading && Changed) Changed(); });
    for (QCheckBox * check : {_Ui->alphaTransparencyCheck, _Ui->mipmapsCheck, _Ui->cpuCopyCheck})
        QObject::connect(check, &QCheckBox::toggled, this, [this] { if (!_Loading && Changed) Changed(); });
}
TextureInspector::~TextureInspector() { delete _Ui; }

void TextureInspector::Load(const Json::Value & settings) {
    _Loading = true;
    _Original = settings;
    const auto select = [&](QComboBox * combo, const char * key) {
        combo->setCurrentIndex(combo->findData(QString::fromStdString(settings[key].asString())));
    };
    select(_Ui->colorSpaceCombo, "ColorSpace");
    select(_Ui->alphaSourceCombo, "AlphaSource");
    select(_Ui->filterCombo, "FilterMode");
    select(_Ui->wrapUCombo, "WrapU");
    select(_Ui->wrapVCombo, "WrapV");
    _Ui->alphaTransparencyCheck->setChecked(settings["AlphaIsTransparency"].asBool());
    _Ui->maxSizeCombo->setCurrentText(QString::number(settings["MaxSize"].asUInt()));
    _Ui->mipmapsCheck->setChecked(settings["GenerateMipmaps"].asBool());
    _Ui->cpuCopyCheck->setChecked(settings["KeepCPUCopy"].asBool());
    _Loading = false;
}

bool TextureInspector::Read(Json::Value & settings, std::string & error) const {
    bool valid = false;
    const uint32_t maxSize = _Ui->maxSizeCombo->currentText().trimmed().toUInt(&valid);
    if (!valid || maxSize == 0) { error = "最大尺寸必须是大于零的 uint32 整数。"; return false; }
    settings = _Original;
    for (const auto & field : {std::pair{_Ui->colorSpaceCombo,"ColorSpace"}, {_Ui->alphaSourceCombo,"AlphaSource"},
        {_Ui->filterCombo,"FilterMode"}, {_Ui->wrapUCombo,"WrapU"}, {_Ui->wrapVCombo,"WrapV"}}) {
        if (field.first->currentIndex() < 0) { error = "请选择有效的贴图设置。"; return false; }
        settings[field.second] = field.first->currentData().toString().toStdString();
    }
    settings["MaxSize"] = static_cast<Json::UInt>(maxSize);
    settings["AlphaIsTransparency"] = _Ui->alphaTransparencyCheck->isChecked();
    settings["GenerateMipmaps"] = _Ui->mipmapsCheck->isChecked();
    settings["KeepCPUCopy"] = _Ui->cpuCopyCheck->isChecked();
    error.clear();
    return true;
}

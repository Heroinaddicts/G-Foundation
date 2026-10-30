#include "FontInspector.h"
#include "ui_FontInspector.h"

FontInspector::FontInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::FontInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
FontInspector::~FontInspector() { delete _Ui; }
void FontInspector::RefreshEnabled() {}
bool FontInspector::Validate(const Json::Value & settings, std::string & error) const {
    if (settings["FontSize"].asUInt()>512) { error="当前字体管线的字号上限为 512 像素。";return false; }
    if (settings["CharacterPadding"].asUInt()>64) { error="当前字体管线的边距上限为 64 像素。";return false; }
    if (QString::fromStdString(settings["CustomCharacters"].asString()).toUcs4().size()>4096) {
        error="预生成字符不能超过 4096 个。";return false;
    }
    return true;
}

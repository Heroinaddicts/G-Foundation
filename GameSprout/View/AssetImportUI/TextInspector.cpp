#include "TextInspector.h"
#include "ui_TextInspector.h"

TextInspector::TextInspector(QWidget * parent) : ImportSettingsInspector(parent), _Ui(new Ui::TextInspectorForm()) {
    try {
        _Ui->setupUi(this);
        BindFields();
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
TextInspector::~TextInspector() { delete _Ui; }
void TextInspector::RefreshEnabled() {
    const bool text=_Ui->encodingCombo->currentData()!="Binary";
    _Ui->normalizeLineEndingsCheck->setEnabled(text);
    _Ui->stripByteOrderMarkCheck->setEnabled(text);
}
bool TextInspector::Validate(const Json::Value &, std::string &) const { return true; }

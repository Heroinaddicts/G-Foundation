#include "ShaderInspector.h"
#include "ui_ShaderInspector.h"
#include <QRegularExpression>
#include <set>

ShaderInspector::ShaderInspector(QWidget * parent, bool compute) : ImportSettingsInspector(parent), _Ui(new Ui::ShaderInspectorForm()) {
    try {
        _Ui->setupUi(this);
        if (compute) {
            setObjectName("ComputeShaderInspectorForm");
            _Ui->stageCombo->setProperty("settingKey", QString());
            _Ui->stageCombo->hide();_Ui->StageLabel->hide();
        }
        BindFields();
    } catch (...) { delete _Ui;_Ui=nullptr;throw; }
}
ShaderInspector::~ShaderInspector() { delete _Ui; }
void ShaderInspector::RefreshEnabled() {
}
bool ShaderInspector::Validate(const Json::Value & settings, std::string & error) const {
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (!identifier.match(QString::fromStdString(settings["EntryPoint"].asString())).hasMatch()) {
        error="入口函数必须是有效的标识符。";return false;
    }
    std::set<std::string> names;
    for(const auto & define:settings["Defines"]) {
        const QString value=QString::fromStdString(define.asString()),name=value.section('=',0,0);
        if(!identifier.match(name).hasMatch() || value.contains('\r') || value.contains(QChar('\0')) || !names.insert(name.toStdString()).second) {
            error="宏名称须是有效且不重复的标识符，可使用 NAME=value。";return false;
        }
    }
    return true;
}

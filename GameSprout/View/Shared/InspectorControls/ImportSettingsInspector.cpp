#include "ImportSettingsInspector.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <cmath>
#include <limits>

void ImportSettingsInspector::BindFields() {
    for (QWidget * field : findChildren<QWidget *>()) {
        if (field->property("settingKey").toString().isEmpty()) continue;
        _Fields.push_back(field);
        if (auto * check = qobject_cast<QCheckBox *>(field))
            QObject::connect(check, &QCheckBox::toggled, this, [this] { NotifyChanged(); });
        else if (auto * combo = qobject_cast<QComboBox *>(field)) {
            const QStringList values = combo->property("values").toStringList();
            Q_ASSERT(values.size() == combo->count());
            for (int i = 0; i < combo->count(); ++i) combo->setItemData(i, values.value(i));
            QObject::connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { NotifyChanged(); });
        } else if (auto * number = qobject_cast<QDoubleSpinBox *>(field))
            QObject::connect(number, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { NotifyChanged(); });
        else if (auto * number = qobject_cast<QSpinBox *>(field))
            QObject::connect(number, qOverload<int>(&QSpinBox::valueChanged), this, [this] { NotifyChanged(); });
        else if (auto * text = qobject_cast<QLineEdit *>(field))
            QObject::connect(text, &QLineEdit::textChanged, this, [this] { NotifyChanged(); });
        else if (auto * text = qobject_cast<QPlainTextEdit *>(field))
            QObject::connect(text, &QPlainTextEdit::textChanged, this, [this] { NotifyChanged(); });
    }
}
void ImportSettingsInspector::NotifyChanged() {
    if (_Loading) return;
    RefreshEnabled();
    if (Changed) Changed();
}
void ImportSettingsInspector::Load(const Json::Value & settings) {
    _Loading = true;
    _Original = settings;
    for (QWidget * field : _Fields) {
        const Json::Value & value = settings[field->property("settingKey").toString().toStdString()];
        if (auto * check = qobject_cast<QCheckBox *>(field)) check->setChecked(value.asBool());
        else if (auto * combo = qobject_cast<QComboBox *>(field)) combo->setCurrentIndex(combo->findData(QString::fromStdString(value.asString())));
        else if (auto * number = qobject_cast<QDoubleSpinBox *>(field)) {
            number->setValue(value.asDouble());
            number->setProperty("loadedNumber", number->value());
        } else if (auto * number = qobject_cast<QSpinBox *>(field)) {
            number->setValue(value.asInt());
            number->setProperty("loadedNumber", number->value());
        } else {
            QString text;
            if (field->property("fieldType").toString() == "lines") {
                QStringList lines;
                for (const auto & line : value) lines.push_back(QString::fromStdString(line.asString()));
                text = lines.join('\n');
            } else text = QString::fromStdString(value.asString());
            if (auto * edit = qobject_cast<QLineEdit *>(field)) edit->setText(text);
            else if (auto * edit = qobject_cast<QPlainTextEdit *>(field)) edit->setPlainText(text);
            field->setProperty("loadedText", text);
        }
    }
    _Loading = false;
    RefreshEnabled();
}
bool ImportSettingsInspector::Read(Json::Value & settings, std::string & error) const {
    settings = _Original;
    error.clear();
    for (QWidget * field : _Fields) {
        const std::string key = field->property("settingKey").toString().toStdString();
        if (auto * check = qobject_cast<QCheckBox *>(field)) settings[key] = check->isChecked();
        else if (auto * combo = qobject_cast<QComboBox *>(field)) {
            if (combo->currentIndex() < 0) { error = "请选择有效的 " + key + " 设置。";return false; }
            settings[key] = combo->currentData().toString().toStdString();
        } else if (auto * number = qobject_cast<QDoubleSpinBox *>(field)) {
            // 保留未改动字段的原始数值，避免显示精度造成刚加载就变脏。
            if (number->value() != number->property("loadedNumber").toDouble()) settings[key] = number->value();
        } else if (auto * number = qobject_cast<QSpinBox *>(field)) {
            if (number->value() != number->property("loadedNumber").toInt()) settings[key] = number->value();
        } else {
            QString text;
            if (auto * edit = qobject_cast<QLineEdit *>(field)) text = edit->text();
            else if (auto * edit = qobject_cast<QPlainTextEdit *>(field)) text = edit->toPlainText();
            const QString type = field->property("fieldType").toString();
            if (type == "uint32") {
                bool valid = false;
                const uint32_t number = text.trimmed().toUInt(&valid);
                const uint32_t minimum = field->property("minimumValue").isValid() ? field->property("minimumValue").toUInt() : 1;
                if (!valid || number < minimum) { error = key + " 必须是大于等于 " + std::to_string(minimum) + " 的 uint32 整数。";return false; }
                if (text != field->property("loadedText").toString()) settings[key] = static_cast<Json::UInt>(number);
            } else if (type == "positiveFloat") {
                bool valid = false;
                const double number = text.trimmed().toDouble(&valid);
                if (!valid || !std::isfinite(number) || number <= 0 || number > std::numeric_limits<float>::max()
                    || static_cast<float>(number) <= 0) { error = key + " 必须是有效的正浮点数。";return false; }
                if (text != field->property("loadedText").toString()) settings[key] = static_cast<float>(number);
            } else if (text != field->property("loadedText").toString()) {
                if (type == "lines") {
                    settings[key] = Json::Value(Json::arrayValue);
                    for (const QString & line : text.split('\n')) if (!line.trimmed().isEmpty()) settings[key].append(line.trimmed().toStdString());
                } else settings[key] = text.toStdString();
            }
        }
    }
    return Validate(settings, error);
}

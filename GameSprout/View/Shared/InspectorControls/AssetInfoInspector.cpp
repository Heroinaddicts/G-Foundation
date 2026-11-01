#include "AssetInfoInspector.h"
#include "ui_AssetInfoInspector.h"
#include <QHeaderView>
#include <QTreeWidget>

AssetInfoInspector::AssetInfoInspector(QWidget * parent) : QWidget(parent), _Ui(new Ui::AssetInfoInspectorForm()) {
    _Ui->setupUi(this);
    _Ui->propertiesTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _Ui->propertiesTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
}
AssetInfoInspector::~AssetInfoInspector() { delete _Ui; }
void AssetInfoInspector::Append(QTreeWidgetItem * parent, const QString & name, const Json::Value & value) {
    auto * row = new QTreeWidgetItem();
    row->setText(0, name);
    if (parent) parent->addChild(row); else _Ui->propertiesTree->addTopLevelItem(row);
    if (value.isObject()) {
        for (const std::string & key : value.getMemberNames()) Append(row, QString::fromStdString(key), value[key]);
    } else if (value.isArray()) {
        row->setText(1, QString::number(value.size()) + " 项");
        for (Json::ArrayIndex i = 0; i < value.size(); ++i) Append(row, QString::number(i), value[i]);
    } else {
        const QString text = value.isNull() ? QStringLiteral("—") : QString::fromStdString(value.asString());
        row->setText(1, text);
        row->setToolTip(1, text);
    }
}
void AssetInfoInspector::Load(const QString & description, const Json::Value & values) {
    _Ui->descriptionLabel->setText(description);
    _Ui->propertiesTree->clear();
    for (const std::string & key : values.getMemberNames()) Append(nullptr, QString::fromStdString(key), values[key]);
    _Ui->propertiesTree->expandToDepth(0);
}

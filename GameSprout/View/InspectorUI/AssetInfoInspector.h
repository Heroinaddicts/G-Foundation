#pragma once
#include <QWidget>
#include <JsonCpp/json.hpp>
namespace Ui { class AssetInfoInspectorForm; }
class QTreeWidgetItem;

class AssetInfoInspector final : public QWidget {
public:
    explicit AssetInfoInspector(QWidget * parent);
    ~AssetInfoInspector() override;
    AssetInfoInspector(const AssetInfoInspector &) = delete;
    AssetInfoInspector & operator=(const AssetInfoInspector &) = delete;
    void Load(const QString & description, const Json::Value & values);
private:
    void Append(QTreeWidgetItem * parent, const QString & name, const Json::Value & value);
    Ui::AssetInfoInspectorForm * _Ui = nullptr;
};

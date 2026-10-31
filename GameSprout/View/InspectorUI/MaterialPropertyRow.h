#pragma once
#include <QWidget>
#include <JsonCpp/json.hpp>
#include <functional>
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
namespace Ui { class MaterialPropertyRowForm; }

// 每一行由 .ui 创建；属性描述只决定显示哪一页和使用什么取值范围。
class MaterialPropertyRow final : public QWidget {
public:
    explicit MaterialPropertyRow(QWidget * parent = nullptr);
    ~MaterialPropertyRow() override;
    void Load(const Json::Value & definition, const Json::Value & document,
        IAssetsManager *, ILibraryManager *, ISelectionManager *, const std::string & context);
    bool Write(Json::Value & document, std::string & error) const;
    std::function<void()> Changed;
private:
    void Notify();
    void UpdateColor();
    Ui::MaterialPropertyRowForm * _Ui = nullptr;
    Json::Value _Definition;
    Json::Value _Value;
    Json::Value _TextureST;
    bool _Loading = false;
    bool _IntegerValid = true;
};

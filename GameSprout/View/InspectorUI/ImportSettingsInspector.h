#pragma once
#include <QWidget>
#include <JsonCpp/json.hpp>
#include <functional>
#include <vector>

// 面板只持有设置快照。控件、范围及枚举在各自 .ui 定义。
class ImportSettingsInspector : public QWidget {
public:
    explicit ImportSettingsInspector(QWidget * parent) : QWidget(parent) {}
    virtual void Load(const Json::Value & settings);
    virtual bool Read(Json::Value & settings, std::string & error) const;
    std::function<void()> Changed;
protected:
    void BindFields();
    virtual void RefreshEnabled() {}
    virtual bool Validate(const Json::Value &, std::string &) const { return true; }
private:
    void NotifyChanged();
    std::vector<QWidget *> _Fields; // Qt parent owns these generated controls.
    Json::Value _Original;
    bool _Loading = false;
};

#pragma once
#include <QWidget>
#include <JsonCpp/json.hpp>
#include <functional>
#include <string>
#include <vector>

class IModelAnimationEditor : public QWidget {
public:
    explicit IModelAnimationEditor(QWidget* parent) : QWidget(parent) {}
    ~IModelAnimationEditor() override = default;
    virtual void Load(const Json::Value& clips) = 0;
    virtual bool Read(Json::Value& clips, std::string& error) const = 0;
    virtual void SetSources(const std::vector<std::string>& clips, const std::vector<std::string>& nodes) = 0;
    std::function<void()> Changed;
};

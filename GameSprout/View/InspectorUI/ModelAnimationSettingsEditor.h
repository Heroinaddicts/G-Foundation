#pragma once
#include <QWidget>
#include <JsonCpp/json.hpp>
#include <functional>
#include <vector>
namespace Ui{class ModelAnimationSettingsEditorForm;}
class ModelAnimationSettingsEditor final : public QWidget {
public:
    explicit ModelAnimationSettingsEditor(QWidget* parent=nullptr);
    ~ModelAnimationSettingsEditor() override;
    void Load(const Json::Value& clips);
    bool Read(Json::Value& clips,std::string& error) const;
    void SetSources(const std::vector<std::string>& clips,const std::vector<std::string>& nodes);
    std::function<void()> Changed;
private:
    bool StoreCurrent();
    void SelectClip(int row);
    void Rebuild(int row);
    Ui::ModelAnimationSettingsEditorForm* _Ui=nullptr;
    Json::Value _Clips{Json::arrayValue};
    bool _Loading=false;
    int _Row=-1;
};

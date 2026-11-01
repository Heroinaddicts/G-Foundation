#pragma once
#include "IModelAnimationEditor.h"
#include <JsonCpp/json.hpp>
#include <functional>
#include <vector>
namespace Ui{class ModelAnimationSettingsEditorForm;}
class ModelAnimationSettingsEditor final : public IModelAnimationEditor {
public:
    explicit ModelAnimationSettingsEditor(QWidget* parent=nullptr);
    ~ModelAnimationSettingsEditor() override;
    void Load(const Json::Value& clips) override;
    bool Read(Json::Value& clips,std::string& error) const override;
    void SetSources(const std::vector<std::string>& clips,const std::vector<std::string>& nodes) override;
private:
    bool StoreCurrent();
    void SelectClip(int row);
    void Rebuild(int row);
    Ui::ModelAnimationSettingsEditorForm* _Ui=nullptr;
    Json::Value _Clips{Json::arrayValue};
    bool _Loading=false;
    int _Row=-1;
};

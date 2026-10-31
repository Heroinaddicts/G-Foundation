#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class ModelInspectorForm; }
class ModelInspector final : public ImportSettingsInspector {
public:
    explicit ModelInspector(QWidget * parent);
    ~ModelInspector() override;
    void Load(const Json::Value&) override;
    bool Read(Json::Value&,std::string&) const override;
    std::function<void()> ConfigureRig;
    void SetAnimationSources(const std::vector<std::string>& clips,const std::vector<std::string>& nodes);
    ModelInspector(const ModelInspector &) = delete;
    ModelInspector & operator=(const ModelInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::ModelInspectorForm * _Ui = nullptr;
};

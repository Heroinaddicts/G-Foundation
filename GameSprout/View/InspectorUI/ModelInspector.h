#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class ModelInspectorForm; }
class ModelInspector final : public ImportSettingsInspector {
public:
    explicit ModelInspector(QWidget * parent);
    ~ModelInspector() override;
    ModelInspector(const ModelInspector &) = delete;
    ModelInspector & operator=(const ModelInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::ModelInspectorForm * _Ui = nullptr;
};

#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class ShaderInspectorForm; }
class ShaderInspector final : public ImportSettingsInspector {
public:
    explicit ShaderInspector(QWidget * parent, bool compute = false);
    ~ShaderInspector() override;
    ShaderInspector(const ShaderInspector &) = delete;
    ShaderInspector & operator=(const ShaderInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::ShaderInspectorForm * _Ui = nullptr;
};

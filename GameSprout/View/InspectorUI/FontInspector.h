#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class FontInspectorForm; }
class FontInspector final : public ImportSettingsInspector {
public:
    explicit FontInspector(QWidget * parent);
    ~FontInspector() override;
    FontInspector(const FontInspector &) = delete;
    FontInspector & operator=(const FontInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::FontInspectorForm * _Ui = nullptr;
};

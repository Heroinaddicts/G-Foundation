#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class TextInspectorForm; }
class TextInspector final : public ImportSettingsInspector {
public:
    explicit TextInspector(QWidget * parent);
    ~TextInspector() override;
    TextInspector(const TextInspector &) = delete;
    TextInspector & operator=(const TextInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::TextInspectorForm * _Ui = nullptr;
};

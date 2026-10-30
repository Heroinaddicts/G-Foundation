#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class AudioInspectorForm; }
class AudioInspector final : public ImportSettingsInspector {
public:
    explicit AudioInspector(QWidget * parent);
    ~AudioInspector() override;
    AudioInspector(const AudioInspector &) = delete;
    AudioInspector & operator=(const AudioInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::AudioInspectorForm * _Ui = nullptr;
};

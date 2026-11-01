#pragma once
#include "ImportSettingsInspector.h"
namespace Ui { class VideoInspectorForm; }
class VideoInspector final : public ImportSettingsInspector {
public:
    explicit VideoInspector(QWidget * parent);
    ~VideoInspector() override;
    VideoInspector(const VideoInspector &) = delete;
    VideoInspector & operator=(const VideoInspector &) = delete;
protected:
    void RefreshEnabled() override;
    bool Validate(const Json::Value & settings, std::string & error) const override;
private:
    Ui::VideoInspectorForm * _Ui = nullptr;
};

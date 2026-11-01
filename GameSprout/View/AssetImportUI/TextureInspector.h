#pragma once
#include "ImportSettingsInspector.h"
#include <JsonCpp/json.hpp>
#include <functional>
namespace Ui { class TextureInspectorForm; }

class TextureInspector final : public ImportSettingsInspector {
public:
    explicit TextureInspector(QWidget * parent);
    ~TextureInspector() override;
    TextureInspector(const TextureInspector &) = delete;
    TextureInspector & operator=(const TextureInspector &) = delete;
    void Load(const Json::Value & settings) override;
    bool Read(Json::Value & settings, std::string & error) const override;
private:
    Ui::TextureInspectorForm * _Ui = nullptr;
    Json::Value _Original;
    bool _Loading = false;
};

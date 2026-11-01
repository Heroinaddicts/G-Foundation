#pragma once
#include <QWidget>
#include <JsonCpp/json.hpp>
#include <functional>
#include <vector>
namespace Ui {class AnimationClipOptionsEditorForm;}

// View draft for one clip. Pipeline owns semantic validation and extraction.
class AnimationClipOptionsEditor final : public QWidget {
public:
    explicit AnimationClipOptionsEditor(QWidget* parent=nullptr);
    ~AnimationClipOptionsEditor() override;
    void Load(const Json::Value& options);
    bool Read(Json::Value& options,std::string& error) const;
    void SetNodePaths(const std::vector<std::string>& paths);
    std::function<void()> Changed;
private:
    void NotifyChanged();
    Ui::AnimationClipOptionsEditorForm* _Ui=nullptr;
    Json::Value _Original;
    bool _Loading=false;
    bool _Touched=false;
};

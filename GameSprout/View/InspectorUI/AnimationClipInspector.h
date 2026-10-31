#pragma once
#include "ImportSettingsInspector.h"
class IAssetsManager;class ILibraryManager;class ISelectionManager;
namespace Ui{class AnimationClipInspectorForm;}
class AnimationClipInspector final : public ImportSettingsInspector {
public:
    explicit AnimationClipInspector(QWidget* parent);
    ~AnimationClipInspector() override;
    void Load(const Json::Value&) override;
    bool Read(Json::Value&,std::string&) const override;
    void BindSourceAvatar(IAssetsManager*,ILibraryManager*,ISelectionManager*,const std::string& key);
    void SetNodePaths(const std::vector<std::string>&);
private:
    Ui::AnimationClipInspectorForm* _Ui=nullptr;
};

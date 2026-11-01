#pragma once
#include "InspectorPanel.h"
class IAnimationUI;
class IAnimationEditor;
class IRuntimeAssets;
namespace Ui { class AnimationAssetPanelForm; }

// 动画资源页仅持有身份与控件草稿，持久化由 AnimationEditor/AssetsManager 完成。
class AnimationAssetPanel final : public InspectorPanel {
public:
    AnimationAssetPanel(const InspectorServices&, IAnimationUI*, QWidget* parent);
    ~AnimationAssetPanel() override;
    bool Load(const InspectorTarget&, bool changed, std::string& error) override;
    InspectorPresentation Describe() const override { return _Presentation; }
    bool HasPendingChanges() const override;
    bool Validate(std::string& error) const override;
    bool Apply(std::string& error) override;
    bool Revert(std::string& error) override;
private:
    bool LoadImportSettings(IAssetMeta&, bool changed, std::string& error);
    void BindAnimationSources(IAssetMeta&);
    Ui::AnimationAssetPanelForm* _Ui = nullptr;
    IAnimationUI* _AnimationUI = nullptr;
    IAnimationEditor* _Authoring = nullptr;
    IRuntimeAssets* _Resources = nullptr;
    InspectorTarget _Target;
    InspectorPresentation _Presentation;
    std::string _Baseline;
    bool _ImportSettings = false;
};

#pragma once
#include "RuntimeAnimationData.h"
#include <QWidget>
#include <functional>
namespace Ui{class AnimationClipPreviewInspectorForm;}
class AnimationClipPreviewInspector final : public QWidget {
public:
    explicit AnimationClipPreviewInspector(QWidget* parent=nullptr);
    ~AnimationClipPreviewInspector() override;
    void Load(const GameSproutRuntime::AnimationClipData&);
    std::function<void()> EditSource;
private:
    Ui::AnimationClipPreviewInspectorForm* _Ui=nullptr;
};

#pragma once
#include "RuntimeAnimationApi.h"
#include <QWidget>
#include <functional>
namespace Ui{class AnimatorIKEditorForm;}
class AnimatorIKEditor final:public QWidget{
public:
    explicit AnimatorIKEditor(QWidget* parent=nullptr);~AnimatorIKEditor()override;
    std::function<GameSproutRuntime::IAnimatorComponent*()> Resolve;
    void ResetContext();
    void SetAvailable(bool);
private:
    void LoadGoal();void ApplyGoal();void ApplyLookAt();
    Ui::AnimatorIKEditorForm* _Ui=nullptr;
};

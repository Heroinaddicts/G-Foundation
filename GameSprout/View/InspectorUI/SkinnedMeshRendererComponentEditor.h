#pragma once
#include "ComponentEditor.h"
namespace Ui{class SkinnedMeshRendererComponentEditorForm;}
class SkinnedMeshRendererComponentEditor final : public ComponentEditor {
public:
    explicit SkinnedMeshRendererComponentEditor(QWidget*);
    ~SkinnedMeshRendererComponentEditor() override;
protected:
    void ReadValues(GameSproutRuntime::IComponent&) override;
private:
    void CommitAnimator();
    Ui::SkinnedMeshRendererComponentEditorForm* _Ui=nullptr;
};

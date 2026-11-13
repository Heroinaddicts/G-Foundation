#pragma once
#include "ComponentEditor.h"
class ILightingAuthoring;
namespace Ui {class LightComponentEditorForm;}
class LightComponentEditor final : public ComponentEditor {
public:
    LightComponentEditor(QWidget*,ILightingAuthoring*);
    ~LightComponentEditor() override;
private:
    void ReadValues(GameSproutRuntime::IComponent&) override;
    void CommitValues(QWidget*);
    void RebuildShadows();
    void ChooseColor();
    void UpdateColor();
    Ui::LightComponentEditorForm* _Ui;
    ILightingAuthoring* _Authoring;
};

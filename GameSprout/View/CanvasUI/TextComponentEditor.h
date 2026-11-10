#pragma once
#include "ComponentEditor.h"

class IUIAuthoring;
namespace Ui {class TextComponentEditorForm;}

class TextComponentEditor final : public ComponentEditor {
public:
    TextComponentEditor(QWidget* parent,IUIAuthoring* authoring);
    ~TextComponentEditor() override;
private:
    void ContextChanged() override;
    void ReadValues(GameSproutRuntime::IComponent&) override;
    void CommitField(QWidget*);
    Ui::TextComponentEditorForm* _Ui=nullptr;
    IUIAuthoring* _Authoring=nullptr;
};

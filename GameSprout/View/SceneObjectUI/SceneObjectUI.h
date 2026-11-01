#pragma once
#include "ISceneObjectUI.h"
#include "InspectorPanel.h"
class IInspectorUI;

class SceneObjectUI final : public ISceneObjectUI, public IInspectorExtension {
public:
    ~SceneObjectUI() override { Release(nullptr); }
    void Show() override;
    void Hide() override {}
    bool CanInspect(const InspectorServices&, const InspectorTarget&) const override;
    InspectorPanel* CreatePanel(const InspectorServices&, QWidget*) override;
    ComponentEditor* CreateComponentEditor(const std::type_info&, QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override { return _Inspector != nullptr; }
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    IInspectorUI* _Inspector = nullptr;
    bool _Registered = false;
};

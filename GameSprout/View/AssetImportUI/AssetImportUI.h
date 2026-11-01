#pragma once
#include "IAssetImportUI.h"
#include "InspectorPanel.h"
class IInspectorUI;
class AssetImportUI final : public IAssetImportUI, public IInspectorExtension {
public:
    ~AssetImportUI() override { Release(nullptr); }
    void Show() override;
    void Hide() override {}
    int Priority() const override { return -100; }
    bool CanInspect(const InspectorServices&, const InspectorTarget&) const override;
    InspectorPanel* CreatePanel(const InspectorServices&, QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override { return _Inspector != nullptr; }
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    IInspectorUI* _Inspector = nullptr;
    bool _Registered = false;
};

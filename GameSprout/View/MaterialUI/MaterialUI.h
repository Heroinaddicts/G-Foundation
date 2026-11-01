#pragma once
#include "IMaterialUI.h"
#include "InspectorPanel.h"
class IInspectorUI;

// 材质 View 扩展；只拥有界面实现，不负责源资产登记或预处理。
class MaterialUI final : public IMaterialUI, public IInspectorExtension {
public:
    ~MaterialUI() override;
    void Show() override;
    void Hide() override {}
    int Priority() const override { return 100; }
    bool CanInspect(const InspectorServices&,const InspectorTarget&) const override;
    InspectorPanel* CreatePanel(const InspectorServices&,QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    IInspectorUI* _Inspector=nullptr;
    bool _Registered=false;
};

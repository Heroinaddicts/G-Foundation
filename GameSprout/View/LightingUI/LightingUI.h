#pragma once
#include "Header.h"
#include "InspectorPanel.h"
#include <QPointer>
class ILightingAuthoring;
class IHierarchyUI;
class IInspectorUI;
class IIDEWindow;
class IIDEMenu;
class EnvironmentLightingWindow;
class QMenu;
class QDialog;
namespace Ui {class LightingActionsForm;}
class LightingUI final : public ILightingUI,public IInspectorExtension {
public:
    ~LightingUI() override;
    void Show() override;
    void Hide() override;
    int Priority() const override {return 20;}
    bool CanInspect(const InspectorServices&,const InspectorTarget&) const override {return false;}
    InspectorPanel* CreatePanel(const InspectorServices&,QWidget*) override {return nullptr;}
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void Create();
    static void CreateMenu();
    static void ShowEnvironmentMenu();
    ILightingAuthoring* _Authoring=nullptr;
    IHierarchyUI* _Hierarchy=nullptr;
    IInspectorUI* _Inspector=nullptr;
    IIDEWindow* _IDE=nullptr;
    IIDEMenu* _EnvironmentMenu=nullptr;
    EnvironmentLightingWindow* _EnvironmentWindow=nullptr;
    QMenu* _Actions=nullptr;
    Ui::LightingActionsForm* _Ui=nullptr;
    QPointer<QDialog> _ErrorDialog;
    bool _Registered=false;
};

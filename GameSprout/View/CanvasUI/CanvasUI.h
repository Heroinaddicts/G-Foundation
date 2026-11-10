#pragma once
#include "Header.h"
#include "InspectorPanel.h"
#include <QPointer>

class IUIAuthoring;
class IHierarchyUI;
class IInspectorUI;
class QMenu;
class QDialog;
enum class UIControlTemplate;
namespace Ui {class CanvasActionsForm;}

class CanvasUI final : public ICanvasUI,public IInspectorExtension {
public:
    ~CanvasUI() override;
    void Show() override {}
    void Hide() override;
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    template<UIControlTemplate Type> static void CreateMenu();
    void Create(UIControlTemplate);
    void Report(const std::string&);
    IUIAuthoring* _Authoring=nullptr;
    IHierarchyUI* _Hierarchy=nullptr;
    IInspectorUI* _Inspector=nullptr;
    QMenu* _Actions=nullptr;
    Ui::CanvasActionsForm* _Ui=nullptr;
    QPointer<QDialog> _ErrorDialog;
    bool _InspectorRegistered=false;
};

#pragma once
#include "Header.h"
#include "RuntimeBuiltinResources.h"
#include <QPointer>
class IPrimitiveAuthoring;
class IHierarchyUI;
class QMenu;
class QDialog;
namespace Ui {class PrimitiveActionsForm;}
class PrimitiveUI final : public IPrimitiveUI {
public:
    ~PrimitiveUI() override;
    void Show() override {}
    void Hide() override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    template<GameSproutRuntime::PrimitiveType Type> static void CreateMenu();
    void Create(GameSproutRuntime::PrimitiveType);
    void Report(const std::string&);
    IPrimitiveAuthoring* _Authoring=nullptr;
    IHierarchyUI* _Hierarchy=nullptr;
    QMenu* _Actions=nullptr;
    Ui::PrimitiveActionsForm* _Ui=nullptr;
    QPointer<QDialog> _ErrorDialog;
};

#pragma once
#include "Header.h"
#include "InspectorPanel.h"
#include <QPointer>
class IInspectorUI;
class IProjectAssetsUI;
class IPhysicsAuthoring;
class IAssetsManager;
class QMenu;
class QDialog;
class QObject;
namespace Ui {class PhysicsActionsForm;}

class PhysicsUI final:public IPhysicsUI,public IInspectorExtension {
public:
    ~PhysicsUI() override;
    void Show() override;
    void Hide() override;
    int Priority() const override {return 20;}
    bool CanInspect(const InspectorServices&,const InspectorTarget&) const override;
    InspectorPanel* CreatePanel(const InspectorServices&,QWidget*) override;
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Update(GameSproutEditor::GameSproutViewApi*) override {}
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void CreateMaterial();
    static void CreateMaterialMenu();
    IInspectorUI* _Inspector=nullptr;
    IProjectAssetsUI* _ProjectAssets=nullptr;
    IPhysicsAuthoring* _Authoring=nullptr;
    IAssetsManager* _Assets=nullptr;
    QMenu* _Actions=nullptr;
    Ui::PhysicsActionsForm* _Ui=nullptr;
    QObject* _Dispatch=nullptr;
    QPointer<QDialog> _CreateDialog;
    bool _Registered=false;
};

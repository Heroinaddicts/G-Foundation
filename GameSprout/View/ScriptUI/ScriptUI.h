#pragma once
#include "Header.h"
#include "InspectorPanel.h"
#include "IProjectAssetsUI.h"
#include "IScriptEditor.h"
class IInspectorUI;class IAssetsManager;class ICodeEditor;class IConsole;class QDialog;class QMenu;
namespace Ui {class ScriptActionsForm;class CreateScriptDialogForm;}
class ScriptUI final : public IScriptUI,public IInspectorExtension {
public:
    ~ScriptUI() override;
    void Show() override;void Hide() override;
    bool CanInspect(const InspectorServices&,const InspectorTarget&)const override{return false;}
    InspectorPanel* CreatePanel(const InspectorServices&,QWidget*) override{return nullptr;}
    ComponentEditor* CreateComponentEditor(const std::type_info&,QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*)override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override;
    bool Launch(GameSproutEditor::GameSproutViewApi*)override;
    void Release(GameSproutEditor::GameSproutViewApi*)override;
    static void CreateScriptMenu();
    void OpenCreateDialog();
    bool OpenSourceAsset(const ProjectAssetSelection&);
    IInspectorUI* _Inspector=nullptr;
    IProjectAssetsUI* _ProjectAssets=nullptr;
    IScriptEditor* _Scripts=nullptr;
    IAssetsManager* _Assets=nullptr;
    ICodeEditor* _CodeEditor=nullptr;
    IConsole* _Console=nullptr;
    QMenu* _Actions=nullptr;QDialog* _Dialog=nullptr;
    Ui::ScriptActionsForm* _Ui=nullptr;
    Ui::CreateScriptDialogForm* _DialogUi=nullptr;
    bool _Registered=false;
};

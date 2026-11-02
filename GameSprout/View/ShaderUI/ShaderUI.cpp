#include "ShaderUI.h"
#include "IProjectAssetsUI.h"
#include "IShaderAuthoring.h"
#include "IAssetsManager.h"
#include "ui_ShaderUI.h"
#include "ui_CreateShaderDialog.h"
#include "ui_CreateMaterialDialog.h"
#include <QApplication>
#include <QDialog>
#include <QMenu>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QComboBox>

ShaderUI::~ShaderUI(){Release(nullptr);}
void ShaderUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_ShaderUI=this;g_ShaderUIViewApi=editor;
    _ProjectAssets=editor?editor->GetGameSproutView<IProjectAssetsUI>("ProjectAssetsUI"):nullptr;
    _Authoring=editor?editor->GetGameSproutModule<IShaderAuthoring>("ShaderAuthoring"):nullptr;
    _Assets=editor?editor->GetGameSproutModule<IAssetsManager>("AssetsManager"):nullptr;
}
bool ShaderUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Dispatch=new QObject();_Actions=new QMenu();_Ui=new Ui::ShaderActionsForm();_Ui->setupUi(_Actions);return true;
}
bool ShaderUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_ProjectAssets||!_Authoring||!_Assets||!_Ui)return false;
    return _ProjectAssets->RegisterContextAction(this,{_Ui->createMenu->title().toStdString(),_Ui->shaderMenu->title().toStdString(),_Ui->createUnlitAction->text().toStdString()},&CreateShaderMenu)
        &&_ProjectAssets->RegisterContextAction(this,{_Ui->createMenu->title().toStdString(),_Ui->createMaterialAction->text().toStdString()},&CreateMaterialMenu);
}
void ShaderUI::LaunchFinished(GameSproutEditor::GameSproutViewApi*) {}
void ShaderUI::Show(){if(_CreateDialog){_CreateDialog->show();_CreateDialog->raise();}}
void ShaderUI::Hide(){if(_CreateDialog)_CreateDialog->hide();}
void ShaderUI::Update(GameSproutEditor::GameSproutViewApi*) {}
void ShaderUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_ProjectAssets)_ProjectAssets->UnregisterExtensions(this);
    delete _CreateDialog.data();_CreateDialog=nullptr;
    delete _Dispatch;_Dispatch=nullptr;delete _Actions;_Actions=nullptr;delete _Ui;_Ui=nullptr;
    _ProjectAssets=nullptr;_Authoring=nullptr;_Assets=nullptr;
    if(g_ShaderUI==this)g_ShaderUI=nullptr;g_ShaderUIViewApi=nullptr;
}
void ShaderUI::CreateShaderMenu(){if(g_ShaderUI)g_ShaderUI->CreateAsset(true);}
void ShaderUI::CreateMaterialMenu(){if(g_ShaderUI)g_ShaderUI->CreateAsset(false);}
void ShaderUI::CreateAsset(bool shader) {
    if(!_ProjectAssets||!_Authoring||!_Assets||!_Dispatch)return;
    if(_CreateDialog){Show();return;}
    const auto context=_ProjectAssets->GetActionContext();
    GameSproutRuntime::ResourceReference selectedShader;std::string selectedPath;
    if(!shader&&context.Selection.size()==1) {
        const auto& selection=context.Selection.front();
        const auto* meta=_Assets->QueryAsset(Guid(selection.GuidString));
        if(meta&&meta->_ImporterType==eAssetImporterType::Shader) {
            selectedShader={selection.GuidString,selection.LocalID};selectedPath=meta->GetPath();
        }
    }
    auto* dialog=new QDialog(QApplication::activeWindow());_CreateDialog=dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowModality(Qt::WindowModal);
    QLineEdit* name=nullptr;QLabel* directory=nullptr;QLabel* error=nullptr;QDialogButtonBox* buttons=nullptr;QComboBox* preset=nullptr;
    if(shader) {
        Ui::CreateShaderDialogForm form;form.setupUi(dialog);
        name=form.nameEdit;directory=form.directoryLabel;error=form.errorLabel;buttons=form.buttonBox;
    }else {
        Ui::CreateMaterialDialogForm form;form.setupUi(dialog);
        name=form.nameEdit;directory=form.directoryLabel;error=form.errorLabel;buttons=form.buttonBox;
        preset=form.presetCombo;
        if(!selectedPath.empty()){form.shaderLabel->setText(QString::fromStdString(selectedPath));preset->hide();}
        else {form.shaderLabel->hide();preset->setCurrentIndex(1);}
    }
    directory->setText(context.Directory.empty()?QStringLiteral("Assets"):QStringLiteral("Assets/")+QString::fromStdString(context.Directory));
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this,dialog]{if(_CreateDialog==dialog)_CreateDialog=nullptr;});
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,name,error,context,selectedShader,shader,preset]{
        if(!_ProjectAssets||!_Authoring)return;
        std::string path,message;
        const bool created=shader?_Authoring->CreateUnlitShader(context.Directory,name->text().trimmed().toStdString(),path,message)
            :selectedShader.Guid.empty()&&preset
                ?_Authoring->CreateBuiltinMaterial(context.Directory,name->text().trimmed().toStdString(),preset->currentIndex()==1?BuiltinMaterialPreset::Lit:BuiltinMaterialPreset::Preview,path,message)
                :_Authoring->CreateMaterial(context.Directory,name->text().trimmed().toStdString(),selectedShader,path,message);
        if(!created){error->setText(QString::fromStdString(message));return;}
        _ProjectAssets->NotifyAssetCreated(path);dialog->accept();
    });
    name->selectAll();dialog->open();
}

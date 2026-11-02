#include "ProjectSettingsUI.h"
#include "ProjectSettingsModels.h"
#include "IIDEWindow.h"
#include "ui_ProjectSettingsUI.h"
#include <QApplication>
#include <QDialog>
#include <QHeaderView>
#include <QPushButton>
#include <QAction>
#include <limits>

ProjectSettingsUI::~ProjectSettingsUI(){Release(nullptr);}
void ProjectSettingsUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) {
    g_Self=this;g_GameSproutEditor=api;if(!api)return;
    _Settings=api->GetGameSproutModule<IProjectSettings>("ProjectSettings");
    _Play=api->GetGameSproutModule<IPlayMode>("PlayMode");
    _IDE=api->GetGameSproutView<IIDEWindow>("IDEWindow");
}
bool ProjectSettingsUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Window)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Window=new QDialog();_Ui=new Ui::ProjectSettingsForm();_Ui->setupUi(_Window);
    _Tags=new ProjectTagsModel(_Draft,_Window);_Layers=new ProjectLayersModel(_Draft,_Window);_Collision=new ProjectCollisionModel(_Draft,_Window);
    _Ui->tagsTable->setModel(_Tags);_Ui->layersTable->setModel(_Layers);_Ui->collisionTable->setModel(_Collision);
    _Ui->tagsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _Ui->layersTable->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    _Ui->layersTable->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    _Ui->tagsTable->verticalHeader()->setDefaultSectionSize(26);_Ui->layersTable->verticalHeader()->setDefaultSectionSize(26);
    _Ui->collisionTable->verticalHeader()->setDefaultSectionSize(28);
    _Ui->collisionTable->horizontalHeader()->setDefaultSectionSize(110);
    _Tags->Edited=[this]{SetDirty();};
    _Layers->Edited=[this]{_Collision->RefreshLayers();SetDirty();};
    _Collision->Edited=[this]{SetDirty();};
    QObject::connect(_Ui->addTagButton,&QPushButton::clicked,_Window,[this]{AddTag();});
    QObject::connect(_Ui->addTagEdit,&QLineEdit::returnPressed,_Window,[this]{AddTag();});
    QObject::connect(_Ui->removeTagButton,&QPushButton::clicked,_Window,[this]{RemoveTag();});
    QObject::connect(_Ui->applyButton,&QPushButton::clicked,_Window,[this]{Apply();});
    QObject::connect(_Ui->revertButton,&QPushButton::clicked,_Window,[this]{ReadSnapshot();});
    QObject::connect(_Ui->closeButton,&QPushButton::clicked,_Window,&QDialog::hide);
    QObject::connect(_Ui->enableAllButton,&QPushButton::clicked,_Window,[this]{if(_Editable){_Draft.CollisionMatrix.fill(GameSproutRuntime::AllLayers);_Collision->Refresh();SetDirty();}});
    QObject::connect(_Ui->disableAllButton,&QPushButton::clicked,_Window,[this]{if(_Editable){_Draft.CollisionMatrix.fill(0);_Collision->Refresh();SetDirty();}});
    return true;
}
bool ProjectSettingsUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Settings||!_IDE||!_Window)return false;
    _SettingsSubscribed=_Settings->RegisterSettingsListener(this);if(!_SettingsSubscribed)return false;
    if(_Play){_PlaySubscribed=_Play->RegisterPlayModeListener(this);if(!_PlaySubscribed)return false;OnPlayModeStateChanged(_Play->GetState());}
    ReadSnapshot();
    auto* bar=_IDE->GetMenuBar();auto* edit=bar?bar->CreateChildMenu(_Ui->editMenuAction->text()):nullptr;
    _Menu=edit?edit->CreateChildMenu(_Ui->projectSettingsAction->text()):nullptr;
    if(!_Menu)return false;_Menu->SetClickCallback(&ShowMenu);_Menu->SetEnabled(true);return true;
}
void ProjectSettingsUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_Menu){_Menu->SetClickCallback(nullptr);_Menu->SetEnabled(false);}_Menu=nullptr;
    if(_SettingsSubscribed&&_Settings)_Settings->UnregisterSettingsListener(this);
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _SettingsSubscribed=_PlaySubscribed=false;
    delete _Window.data();_Window=nullptr;delete _Ui;_Ui=nullptr;_Tags=nullptr;_Layers=nullptr;_Collision=nullptr;
    _Settings=nullptr;_Play=nullptr;_IDE=nullptr;_Dirty=false;
    if(g_Self==this)g_Self=nullptr;g_GameSproutEditor=nullptr;
}
void ProjectSettingsUI::ShowMenu(){if(g_Self)g_Self->Show();}
void ProjectSettingsUI::Show(){if(_Window){_Window->show();_Window->raise();_Window->activateWindow();}}
void ProjectSettingsUI::Hide(){if(_Window)_Window->hide();}
void ProjectSettingsUI::Error(const std::string& error){if(_Ui){_Ui->errorLabel->setText(QString::fromStdString(error));_Ui->errorLabel->setVisible(!error.empty());}}
void ProjectSettingsUI::RefreshModels(){_Tags->Refresh();_Layers->Refresh();_Collision->RefreshLayers();UpdateEnabled();}
void ProjectSettingsUI::ReadSnapshot(){if(!_Settings||!_Ui)return;_Draft=_Settings->GetSettings();_Revision=_Settings->GetRevision();_Dirty=false;Error({});RefreshModels();}
void ProjectSettingsUI::SetDirty(){_Dirty=true;Error({});UpdateEnabled();}
void ProjectSettingsUI::UpdateEnabled(){
    if(!_Ui)return;_Tags->SetEditable(_Editable);_Layers->SetEditable(_Editable);_Collision->SetEditable(_Editable);
    _Ui->addTagEdit->setEnabled(_Editable);_Ui->addTagButton->setEnabled(_Editable);_Ui->removeTagButton->setEnabled(_Editable);
    _Ui->enableAllButton->setEnabled(_Editable);_Ui->disableAllButton->setEnabled(_Editable);
    _Ui->applyButton->setEnabled(_Editable&&_Dirty);_Ui->revertButton->setEnabled(_Dirty);
    _Ui->playNotice->setVisible(!_Editable);
}
void ProjectSettingsUI::OnProjectSettingsChanged(const GameSproutRuntime::RuntimeProjectSettings& data,std::uint64_t revision){
    if(_Dirty&&!_Saving){Error("项目设置已被其他操作修改，请先还原再编辑。");return;}
    _Draft=data;_Revision=revision;_Dirty=false;Error({});if(_Ui)RefreshModels();
}
void ProjectSettingsUI::OnPlayModeStateChanged(const PlayModeState& state){_Editable=state.State==EditorPlayState::Edit;UpdateEnabled();}
void ProjectSettingsUI::AddTag(){
    if(!_Editable||!_Ui)return;const auto name=_Ui->addTagEdit->text().trimmed().toStdString();
    if(name.empty()){Error("请输入 Tag 名称。");return;}
    for(const auto& tag:_Draft.Tags)if(tag.Name==name){Error("已存在同名 Tag。");return;}
    if(_Draft.NextTagID==std::numeric_limits<std::uint64_t>::max()){Error("Tag 身份已用尽。");return;}
    _Draft.Tags.push_back({_Draft.NextTagID++,name});_Ui->addTagEdit->clear();_Tags->Refresh();SetDirty();
}
void ProjectSettingsUI::RemoveTag(){
    if(!_Editable||!_Ui)return;const int row=_Ui->tagsTable->currentIndex().row();
    if(row<0||static_cast<std::size_t>(row)>=_Draft.Tags.size())return;
    if(!_Draft.Tags[static_cast<std::size_t>(row)].ID){Error("Untagged 不能删除。");return;}
    _Draft.Tags.erase(_Draft.Tags.begin()+row);_Tags->Refresh();SetDirty();
}
void ProjectSettingsUI::Apply(){
    if(!_Editable||!_Dirty||!_Settings)return;
    if(QWidget* focused=QApplication::focusWidget())focused->clearFocus();
    std::string error;_Saving=true;
    const bool saved=_Settings->SaveSettings(_Draft,_Revision,error);_Saving=false;
    if(!saved){Error(error);return;}ReadSnapshot();
}

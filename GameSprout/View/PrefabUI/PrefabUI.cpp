#include "PrefabUI.h"
#include "IPrefabStageUI.h"
#include "PrefabInstanceSection.h"
#include "ui_PrefabActions.h"
#include "ui_PrefabSaveDialog.h"
#include "ui_PrefabMessageDialog.h"
#include "ui_PrefabAssetInspector.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QMenu>
#include <QPushButton>

namespace {
PrefabUI* ActiveView=nullptr;
class PrefabAssetInspector final : public InspectorPanel {
public:
    PrefabAssetInspector(const InspectorServices& services,QWidget* parent):InspectorPanel(services,parent){
        _Ui.setupUi(this);
        QObject::connect(_Ui.openStageButton,&QPushButton::clicked,this,[this]{
            if(!_Guid.empty()&&_Services.Editor)if(auto* view=_Services.Editor->GetGameSproutView<IPrefabStageUI>("PrefabStageUI"))view->OpenPrefab(_Guid);
        });
    }
    bool Load(const InspectorTarget& target,bool,std::string& error) override {
        error.clear();_Path.clear();
        auto* meta=target.Assets.size()==1&&_Services.Assets?_Services.Assets->QueryAsset(Guid(target.Assets.front().GuidString)):nullptr;
        if(!meta){error="Prefab 资源已移除。";return false;}
        _Guid=meta->_Guid.GetGuidString();_Path=QString::fromStdString(meta->GetPath());_Ui.sourcePathLabel->setText(_Path);
        _Ui.openStageButton->setEnabled(_Services.Editor&&_Services.Editor->GetGameSproutView<IPrefabStageUI>("PrefabStageUI"));return true;
    }
    InspectorPresentation Describe() const override {
        InspectorPresentation result;result.Name=QFileInfo(_Path).fileName();result.Path=_Path;result.Type=QStringLiteral("Prefab");return result;
    }
private:
    Ui::PrefabAssetInspectorForm _Ui;
    QString _Path;
    std::string _Guid;
};
}
PrefabUI::~PrefabUI(){Release(nullptr);}
void PrefabUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* editor) {
    ActiveView=this;if(!editor)return;
    GetGameSproutView(editor,_Inspector,InspectorUI);GetGameSproutView(editor,_Hierarchy,HierarchyUI);
    GetGameSproutView(editor,_ProjectAssets,ProjectAssetsUI);
    GetGameSproutModule(editor,_Prefabs,PrefabManager);GetGameSproutModule(editor,_Selection,SelectionManager);
    GetGameSproutModule(editor,_Assets,AssetsManager);GetGameSproutModule(editor,_Library,LibraryManager);
    GetGameSproutModule(editor,_Scenes,SceneManager);GetGameSproutModule(editor,_Play,PlayMode);
    GetGameSproutModule(editor,_Scripts,ScriptEditor);
}
bool PrefabUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if(_Actions)return true;
    if(!qobject_cast<QApplication*>(QCoreApplication::instance()))return false;
    _Actions=new QMenu();_ActionsUi=new Ui::PrefabActionsForm();_ActionsUi->setupUi(_Actions);return true;
}
bool PrefabUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    if(!_Actions||!_Inspector||!_Hierarchy||!_ProjectAssets||!_Prefabs||!_Selection||!_Assets||!_Library||!_Scenes)return false;
    _Registered=_Inspector->RegisterExtension(this);if(!_Registered)return false;
    _SceneSubscribed=_Scenes->RegisterSceneListener(this);
    if(_Play)_PlaySubscribed=_Play->RegisterPlayModeListener(this);
    if(!_SceneSubscribed||(_Play&&!_PlaySubscribed))return false;
    if(!_ProjectAssets->RegisterContextAction(this,{_Actions->title().toStdString(),_ActionsUi->instantiatePrefabAction->text().toStdString()},&InstantiateMenu,
        [this](const ProjectAssetActionContext& context){GameSproutRuntime::ResourceReference source;return CanAuthor()&&context.Selection.size()==1&&ResolvePrefab(context.Selection.front(),source);}))return false;
    if(!_ProjectAssets->RegisterDropHandler(this,[this](const SceneObjectDropContext& context){return CanDropObject(context);},
        [this](const SceneObjectDropContext& context,std::string& error){return DropObject(context,error);}))return false;
    if(!_Hierarchy->RegisterAssetDropHandler(this,[this](const HierarchyAssetDropContext& context){return CanDropAsset(context);},
        [this](const HierarchyAssetDropContext& context,std::string& error){return DropAsset(context,error);}))return false;
    return _Hierarchy->RegisterContextAction(this,{_Actions->title().toStdString(),_ActionsUi->savePrefabAction->text().toStdString()},&SaveMenu,
        [this](const HierarchyActionContext& context){return CanAuthor()&&ValidSceneTarget(context.SceneID,context.ObjectID);});
}
void PrefabUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_ProjectAssets)_ProjectAssets->UnregisterExtensions(this);
    if(_Hierarchy)_Hierarchy->UnregisterExtensions(this);
    if(_Registered&&_Inspector)_Inspector->UnregisterExtension(this);
    if(_SceneSubscribed&&_Scenes)_Scenes->UnregisterSceneListener(this);
    if(_PlaySubscribed&&_Play)_Play->UnregisterPlayModeListener(this);
    _Registered=_SceneSubscribed=_PlaySubscribed=false;
    delete _SaveDialog.data();_SaveDialog=nullptr;delete _MessageDialog.data();_MessageDialog=nullptr;
    delete _Actions;_Actions=nullptr;delete _ActionsUi;_ActionsUi=nullptr;
    _Inspector=nullptr;_Hierarchy=nullptr;_ProjectAssets=nullptr;_Prefabs=nullptr;_Selection=nullptr;
    _Assets=nullptr;_Library=nullptr;_Scenes=nullptr;_Play=nullptr;_Scripts=nullptr;
    if(ActiveView==this)ActiveView=nullptr;
}
bool PrefabUI::CanInspect(const InspectorServices& services,const InspectorTarget& target) const {
    if(target.Scene.ObjectID||target.Assets.size()!=1)return false;
    auto* meta=services.Assets?services.Assets->QueryAsset(Guid(target.Assets.front().GuidString)):nullptr;
    return meta&&meta->_ImporterType==eAssetImporterType::Prefab;
}
InspectorPanel* PrefabUI::CreatePanel(const InspectorServices& services,QWidget* parent){return new PrefabAssetInspector(services,parent);}
InspectorSection* PrefabUI::CreateSceneObjectSection(const InspectorServices& services,QWidget* parent){return new PrefabInstanceSection(services,parent);}
bool PrefabUI::CanAuthor() const{return _Scenes&&!_Scenes->IsDocumentSessionActive()&&!_Scenes->IsPlaySessionActive()&&(!_Play||_Play->GetState().State==EditorPlayState::Edit);}
bool PrefabUI::ValidSceneTarget(uint64_t sceneID,uint64_t objectID,bool allowRoot) const {
    auto* scene=_Scenes?_Scenes->GetActiveScene():nullptr;
    return scene&&sceneID==scene->GetInstanceID()&&(allowRoot||objectID!=0&&objectID!=scene->GetInstanceID());
}
bool PrefabUI::ResolvePrefab(const ProjectAssetSelection& selected,GameSproutRuntime::ResourceReference& source) const {
    auto* meta=_Assets?_Assets->QueryAsset(Guid(selected.GuidString)):nullptr;if(!meta)return false;
    if(selected.IsSubAsset) {
        auto* asset=_Library?_Library->QueryAsset(meta->_Guid,selected.LocalID):nullptr;
        if(!asset||asset->GetAssetType()!=eAssetType::Prefab)return false;source={selected.GuidString,asset->GetLocalID()};return true;
    }
    if(meta->_ImporterType!=eAssetImporterType::Prefab)return false;
    AssetImportManifest manifest;
    if(_Library&&_Library->GetManifest(meta->_Guid,manifest))for(const auto& asset:manifest.Assets)
        if(asset.Type==eAssetType::Prefab&&asset.SourceKey=="main"){source={selected.GuidString,asset.LocalID};return true;}
    source={selected.GuidString,0};return true;
}
bool PrefabUI::Instantiate(const ProjectAssetSelection& selected,uint64_t sceneID,uint64_t parentID,std::string& error) {
    error.clear();if(!CanAuthor()||!ValidSceneTarget(sceneID,parentID,true)){error="当前场景不可编辑，不能实例化 Prefab。";return false;}
    GameSproutRuntime::ResourceReference source;if(!ResolvePrefab(selected,source)){error="所选资源不是 Prefab。";return false;}
    uint64_t created=0;if(!_Prefabs->InstantiatePrefab(source,parentID,created,error))return false;
    _Selection->SetSceneObjectSelection({sceneID,created});return true;
}
bool PrefabUI::CanDropObject(const SceneObjectDropContext& context) const {
    if(!CanAuthor()||!_Scripts||!context.SessionID||!ValidSceneTarget(context.WorldID,context.ObjectID))return false;
    ScriptObjectTarget current;std::string ignored;
    return _Scripts->ReadObjectTarget(context.ObjectID,current,ignored)&&current.SessionID==context.SessionID
        &&current.WorldID==context.WorldID&&current.ObjectID==context.ObjectID;
}
bool PrefabUI::DropObject(const SceneObjectDropContext& context,std::string& error) {
    if(!CanDropObject(context)){error="拖入的场景对象已过期，或当前不可编辑。";return false;}
    std::string path;if(!_Prefabs->CreatePrefabInDirectory(context.ObjectID,context.Directory,path,error))return false;
    _ProjectAssets->NotifyAssetCreated(path);return true;
}
bool PrefabUI::CanDropAsset(const HierarchyAssetDropContext& context) const {
    GameSproutRuntime::ResourceReference source;return CanAuthor()&&context.Assets.size()==1
        &&ValidSceneTarget(context.SceneID,context.ParentObjectID,true)&&ResolvePrefab(context.Assets.front(),source);
}
bool PrefabUI::DropAsset(const HierarchyAssetDropContext& context,std::string& error) {
    if(!CanDropAsset(context)){error="请拖入一个 Prefab 到当前可编辑场景。";return false;}
    return Instantiate(context.Assets.front(),context.SceneID,context.ParentObjectID,error);
}
void PrefabUI::SaveObject(const HierarchyActionContext& target,const std::string& directory) {
    if(!CanAuthor()||!ValidSceneTarget(target.SceneID,target.ObjectID)){Message(QStringLiteral("右键对象已失效，或当前场景不可编辑。"));return;}
    if(_SaveDialog){_SaveDialog->raise();return;}
    auto* dialog=new QDialog(QApplication::activeWindow());_SaveDialog=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    Ui::PrefabSaveDialog form;form.setupUi(dialog);form.directoryEdit->setText(QString::fromStdString(directory));
    form.objectLabel->setText(QStringLiteral("将右键对象及其子树创建为 Prefab，并连接当前对象。"));form.errorLabel->setTextFormat(Qt::PlainText);
    QObject::connect(form.buttonBox,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(form.buttonBox,&QDialogButtonBox::accepted,dialog,[this,dialog,form,target] {
        if(!CanAuthor()||!ValidSceneTarget(target.SceneID,target.ObjectID)){form.errorLabel->setText(QStringLiteral("创建目标已失效，或当前场景不可编辑。"));return;}
        QString name=form.nameEdit->text().trimmed();
        if(name.isEmpty()||name.contains('/')||name.contains('\\')){form.errorLabel->setText(QStringLiteral("请输入文件名；目录请填写在 Assets 相对目录中。"));return;}
        if(name.endsWith(QStringLiteral(".prefab"),Qt::CaseInsensitive))name.chop(7);name+=QStringLiteral(".prefab");
        const QString folder=form.directoryEdit->text().trimmed();const std::string path=(folder.isEmpty()?name:folder+"/"+name).toStdString();
        std::string error;if(!_Prefabs->CreatePrefab(target.ObjectID,path,error)){form.errorLabel->setText(QString::fromStdString(error));return;}
        _ProjectAssets->NotifyAssetCreated(path);dialog->accept();
    });dialog->open();
}
void PrefabUI::Message(const QString& message) {
    delete _MessageDialog.data();
    auto* dialog=new QDialog(QApplication::activeWindow());_MessageDialog=dialog;dialog->setAttribute(Qt::WA_DeleteOnClose);
    Ui::PrefabMessageDialog form;form.setupUi(dialog);form.messageLabel->setText(message);
    QObject::connect(form.buttonBox,&QDialogButtonBox::rejected,dialog,&QDialog::reject);dialog->open();
}
void PrefabUI::SaveMenu(){if(ActiveView&&ActiveView->_Hierarchy)ActiveView->SaveObject(ActiveView->_Hierarchy->GetActionContext(),ActiveView->_ProjectAssets->GetActionContext().Directory);}
void PrefabUI::InstantiateMenu(){if(ActiveView&&ActiveView->_ProjectAssets){
    const auto context=ActiveView->_ProjectAssets->GetActionContext();auto* scene=ActiveView->_Scenes->GetActiveScene();std::string error;
    if(context.Selection.size()==1&&scene&&!ActiveView->Instantiate(context.Selection.front(),scene->GetInstanceID(),0,error))ActiveView->Message(QString::fromStdString(error));
}}
void PrefabUI::OnActiveSceneChanged(){if(_SaveDialog)_SaveDialog->reject();}
void PrefabUI::OnPlaySessionChanging(bool){if(_SaveDialog)_SaveDialog->reject();}
void PrefabUI::OnPlayModeStateChanged(const PlayModeState& state){if(state.State!=EditorPlayState::Edit&&_SaveDialog)_SaveDialog->reject();}

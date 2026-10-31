#include "ProjectAssetsUI.h"
#include "ProjectAssetsModel.h"
#include "ISceneManager.h"
#include "ISceneEditorUI.h"
#include "IInspectorUI.h"
#include "ILibraryManager.h"
#include "ui_CreateSceneDialog.h"
#include "ui_ProjectAssetsUI.h"
#include <QDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTreeView>
#include <utility>

// 只记录鼠标/键盘右键上下文，不接管弹出逻辑和输入控件的原生菜单。
class ProjectAssetsContextCapture final : public QObject {
public:
    using Callback = std::function<void(QTreeView*, const QModelIndex&)>;
    ProjectAssetsContextCapture(QTreeView* tree, QObject* owner, Callback callback)
        : QObject(owner), _Tree(tree), _Callback(std::move(callback)) {
        tree->installEventFilter(this);
        tree->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress && watched == _Tree->viewport()) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::RightButton) _Callback(_Tree, _Tree->indexAt(mouse->pos()));
        } else if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Menu ||
                (key->key() == Qt::Key_F10 && key->modifiers().testFlag(Qt::ShiftModifier))) {
                _Callback(_Tree, _Tree->currentIndex());
            }
        }
        return false;
    }
private:
    QTreeView* _Tree;
    Callback _Callback;
};

void ProjectAssetsUI::InstallContextCapture() {
    const auto remember = [this](QTreeView* tree, const QModelIndex& index) { RememberContextDirectory(tree,index); };
    new ProjectAssetsContextCapture(_Ui->folderTree, _Dispatch, remember);
    new ProjectAssetsContextCapture(_Ui->assetTree, _Dispatch, remember);
}

void ProjectAssetsUI::RememberContextDirectory(QTreeView*, const QModelIndex& index) {
    const QModelIndex first = index.siblingAtColumn(0);
    _ContextDirectory = first.isValid() && first.data(ProjectAssetsModel::FolderRole).toBool()
        ? first.data(ProjectAssetsModel::PathRole).toString() : _CurrentPath;
    _HasContextDirectory = true;
    _ContextAsset={};
    if (first.isValid()&&!first.data(ProjectAssetsModel::FolderRole).toBool()) {
        _ContextAsset.GuidString=first.data(ProjectAssetsModel::GuidRole).toString().toStdString();
        _ContextAsset.LocalID=first.data(ProjectAssetsModel::LocalIDRole).toULongLong();
        _ContextAsset.IsSubAsset=first.data(ProjectAssetsModel::SubAssetRole).toBool();
    }
}

void ProjectAssetsUI::CreateSceneMenu() {
    if (g_Self) g_Self->ShowCreateSceneDialog();
}

void ProjectAssetsUI::ShowCreateSceneDialog() {
    if (!_Window || !_Scenes) return;
    if (_CreateSceneDialog) {
        _CreateSceneDialog->raise();
        _CreateSceneDialog->activateWindow();
        return;
    }
    const QString directory = _HasContextDirectory ? _ContextDirectory : _CurrentPath;
    _HasContextDirectory = false;
    QDialog* dialog = new QDialog(_Window);
    Ui::CreateSceneDialog ui;
    try { ui.setupUi(dialog); }
    catch (...) { delete dialog; throw; }
    _CreateSceneDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    ui.directoryLabel->setText(directory.isEmpty() ? QStringLiteral("Assets") : QStringLiteral("Assets/") + directory);
    QLineEdit* name = ui.sceneNameEdit;
    QLabel* errorLabel = ui.errorLabel;
    QObject::connect(dialog,&QObject::destroyed,_Dispatch,[this] { _CreateSceneDialog=nullptr; });
    QObject::connect(ui.buttonBox,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
    QObject::connect(ui.buttonBox,&QDialogButtonBox::accepted,dialog,[this,dialog,name,errorLabel,directory] {
        if (!_Scenes) { dialog->reject(); return; }
        std::string path, error;
        if (!_Scenes->CreateSceneAsset(directory.toStdString(),name->text().toStdString(),path,error)) {
            errorLabel->setText(QString::fromStdString(error));
            errorLabel->setVisible(true);
            name->setFocus();
            name->selectAll();
            return;
        }
        // 只创建文件。等待 AssetsManager 的文件事件/Meta，再按身份选中，不自动打开场景。
        _PendingCreatedAssetPath=QString::fromStdString(path);
        const QString parent=QFileInfo(_PendingCreatedAssetPath).path();
        NavigateToDirectory(parent=="." ? "" : parent.toStdString());
        _Dirty=true;
        dialog->accept();
    });
    name->selectAll();
    dialog->open();
    name->setFocus();
}

void ProjectAssetsUI::OpenSceneAsset(const QModelIndex& index) {
    if (!_Assets || !index.isValid()) return;
    IAssetMeta* meta=_Assets->QueryAsset(Guid(index.data(ProjectAssetsModel::GuidRole).toString().toStdString()));
    if (!meta) return;
    if(index.data(ProjectAssetsModel::SubAssetRole).toBool()){
        auto* asset=_Library?_Library->QueryAsset(meta->_Guid,index.data(ProjectAssetsModel::LocalIDRole).toULongLong()):nullptr;
        if(asset&&asset->GetAssetType()==eAssetType::Avatar&&g_GameSproutEditor){auto* inspector=g_GameSproutEditor->GetGameSproutView<IInspectorUI>("InspectorUI");if(inspector)inspector->EditModelRig({meta->_Guid.GetGuidString(),asset->GetLocalID()});}return;
    }
    if (meta->_ImporterType==eAssetImporterType::AnimatorController && _Library && g_GameSproutEditor) {
        AssetImportManifest manifest;
        if (_Library->GetManifest(meta->_Guid,manifest)) for(const auto& asset:manifest.Assets)
            if(asset.Type==eAssetType::AnimatorController&&asset.SourceKey=="main") {
                auto* inspector=g_GameSproutEditor->GetGameSproutView<IInspectorUI>("InspectorUI");
                if(inspector)inspector->EditAnimatorController({meta->_Guid.GetGuidString(),asset.LocalID});
                return;
            }
        return;
    }
    if(meta->_ImporterType==eAssetImporterType::AvatarMask&&_Library&&g_GameSproutEditor){
        AssetImportManifest manifest;if(_Library->GetManifest(meta->_Guid,manifest))for(const auto& asset:manifest.Assets)
            if(asset.Type==eAssetType::AvatarMask&&asset.SourceKey=="main"){
                auto* inspector=g_GameSproutEditor->GetGameSproutView<IInspectorUI>("InspectorUI");
                if(inspector)inspector->EditAvatarMask({meta->_Guid.GetGuidString(),asset.LocalID});return;
            }
        return;
    }
    if(meta->_ImporterType!=eAssetImporterType::Scene||!_Scenes)return;
    if (g_GameSproutEditor) {
        ISceneEditorUI* editor = g_GameSproutEditor->GetGameSproutView<ISceneEditorUI>("SceneEditorUI");
        if (editor) { editor->OpenSceneAsset(meta->GetPath()); return; }
    }
    std::string error;
    if (!_Scenes->OpenScene(meta->GetPath(),error)) {
        _Ui->statusLabel->setText(QString::fromStdString(error));
        _Ui->statusLabel->setToolTip(QString::fromStdString(error));
    } else _Ui->statusLabel->setToolTip(QString());
}

void ProjectAssetsUI::SelectPendingCreatedAsset(const std::vector<IAssetMeta*>& metas) {
    if (_PendingCreatedAssetPath.isEmpty()) return;
    // 导入完成时，用户可能已转去检查场景对象；自动选择不能覆盖新的主动选择。
    if (_Selection && _Selection->GetSceneObjectSelection().ObjectID) {
        _PendingCreatedAssetPath.clear();
        return;
    }
    for (IAssetMeta* meta:metas) {
        if ((meta->_ImporterType!=eAssetImporterType::Scene && meta->_ImporterType!=eAssetImporterType::AnimatorController && meta->_ImporterType!=eAssetImporterType::AvatarMask) || QString::fromStdString(meta->GetPath())!=_PendingCreatedAssetPath) continue;
        const QString parent=QFileInfo(_PendingCreatedAssetPath).path();
        NavigateToDirectory(parent=="." ? "" : parent.toStdString());
        _FolderSelected=false;
        _ExternalSelection={{meta->_Guid.GetGuidString(),0,false}};
        _RestoreExternalSelection=true;
        _PendingCreatedAssetPath.clear();
        return;
    }
}

#include "ProjectAssetsUI.h"
#include "ProjectAssetsModel.h"
#include "IProject.h"
#include "ui_ProjectAssetsUI.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTreeView>
#include <QUrl>
#include <QKeySequence>
#include <algorithm>
#include <utility>

bool ProjectAssetsUI::RegisterAssetOpenHandler(const void* owner,const fAssetOpenHandler& handler) {
    if (!owner || !handler || std::any_of(_OpenHandlers.begin(),_OpenHandlers.end(),
        [owner](const OpenHandler& registered) {return registered.Owner==owner;})) return false;
    _OpenHandlers.push_back({++_NextExtensionID,owner,handler});
    return true;
}

bool ProjectAssetsUI::RegisterContextAction(const void* owner,const std::vector<std::string>& menuPath,
    fContextActionCallback callback,const fContextActionEnabled& enabled) {
    if (!owner || !callback || menuPath.empty() || std::any_of(menuPath.begin(),menuPath.end(),
        [](const std::string& part) {return part.empty();})) return false;
    if (std::any_of(_ContextActions.begin(),_ContextActions.end(),
        [&menuPath](const ContextAction& action) {return action.Path==menuPath;})) return false;
    _ContextActions.push_back({owner,menuPath,callback,enabled,nullptr});
    RegisterExtensionMenus();
    return true;
}

bool ProjectAssetsUI::RegisterAssetShortcut(const void* owner,const std::string& sequence,
    fContextActionCallback callback,const fContextActionEnabled& enabled) {
    if(!owner||!callback||QKeySequence(QString::fromStdString(sequence)).isEmpty())return false;
    if(std::any_of(_KeyboardActions.begin(),_KeyboardActions.end(),[&](const auto& item){return item.Sequence==sequence;}))return false;
    _KeyboardActions.push_back({owner,sequence,callback,enabled});return true;
}
bool ProjectAssetsUI::RouteAssetShortcut(QTreeView*,QKeyEvent* key) {
    const QKeySequence pressed(key->keyCombination());
    const auto actions=_KeyboardActions;
    for(const auto& action:actions) {
        if(pressed!=QKeySequence(QString::fromStdString(action.Sequence)))continue;
        _HasContextDirectory=false;
        const auto context=GetActionContext();
        const auto registered=[&]{return std::any_of(_KeyboardActions.begin(),_KeyboardActions.end(),[&](const auto& item){return item.Owner==action.Owner&&item.Sequence==action.Sequence;});};
        if(registered()&&(!action.Enabled||action.Enabled(context))&&registered())action.Callback();
        key->accept();return true;
    }
    return false;
}

void ProjectAssetsUI::RegisterExtensionMenus() {
    auto* manager=_Docker?_Docker->GetContextMenuManager():nullptr;
    if (!manager) return; // 功能 View 可早于浏览器 Launch；注册项暂存到 Docker 创建后。
    for (auto& action:_ContextActions) {
        if (action.Menu) continue;
        IContextMenu* menu=nullptr;
        for (const auto& part:action.Path) {
            menu=menu?menu->CreateChildMenu(part):manager->CreateMenu(part);
            if (!menu) break;
        }
        action.Menu=menu;
    }
    RefreshContextActions();
}

void ProjectAssetsUI::RefreshContextActions() {
    if (!_Docker) return;
    const auto context=GetActionContext();
    const auto actions=_ContextActions;
    for (const auto& action:actions) {
        if (!action.Menu) continue;
        const bool enabled=!action.Enabled || action.Enabled(context);
        // predicate 可能间接注销功能；不能再调用已注销 owner 的函数。
        const auto current=std::find_if(_ContextActions.begin(),_ContextActions.end(),
            [&action](const ContextAction& registered) {return registered.Owner==action.Owner && registered.Path==action.Path;});
        if (current!=_ContextActions.end() && current->Menu) current->Menu->SetClickCalback(enabled?current->Callback:nullptr);
    }
}

void ProjectAssetsUI::UnregisterExtensions(const void* owner) {
    for (auto& action:_ContextActions) if (_Docker && action.Owner==owner && action.Menu) action.Menu->SetClickCalback(nullptr);
    std::erase_if(_ContextActions,[owner](const ContextAction& action) {return action.Owner==owner;});
    std::erase_if(_OpenHandlers,[owner](const OpenHandler& handler) {return handler.Owner==owner;});
    std::erase_if(_DropHandlers,[owner](const DropHandler& handler) {return handler.Owner==owner;});
    std::erase_if(_KeyboardActions,[owner](const KeyboardAction& action) {return action.Owner==owner;});
}

ProjectAssetActionContext ProjectAssetsUI::GetActionContext() const {
    ProjectAssetActionContext context;
    context.Directory=(_HasContextDirectory?_ContextDirectory:_CurrentPath).toStdString();
    if (_HasContextDirectory) {
        if (!_ContextAsset.GuidString.empty()) {
            const auto selected=GetSelection();
            if(std::find(selected.begin(),selected.end(),_ContextAsset)!=selected.end())context.Selection=selected;
            else context.Selection.push_back(_ContextAsset);
        }
    } else context.Selection=GetSelection();
    return context;
}

void ProjectAssetsUI::NotifyAssetCreated(const std::string& relativePath) {
    if (!_Window || relativePath.empty()) return;
    _PendingCreatedAssetPath=QString::fromStdString(relativePath);
    _HasContextDirectory=false;
    const QString parent=QFileInfo(_PendingCreatedAssetPath).path();
    NavigateToDirectory(parent=="."?"":parent.toStdString());
    QueueRefresh();
}

void ProjectAssetsUI::OpenAssetMenu() {
    if (!g_Self) return;
    const auto context=g_Self->GetActionContext();
    if (!context.Selection.empty()) g_Self->OpenAsset(context.Selection.front());
}

void ProjectAssetsUI::OpenAsset(const QModelIndex& index) {
    if (!index.isValid()) return;
    OpenAsset({index.data(ProjectAssetsModel::GuidRole).toString().toStdString(),
        index.data(ProjectAssetsModel::LocalIDRole).toULongLong(),index.data(ProjectAssetsModel::SubAssetRole).toBool()});
}

void ProjectAssetsUI::OpenAsset(const ProjectAssetSelection& selected) {
    const auto handlers=_OpenHandlers;
    for (const auto& handler:handlers) {
        if (std::none_of(_OpenHandlers.begin(),_OpenHandlers.end(),
            [&handler](const OpenHandler& current) {return current.ID==handler.ID;})) continue;
        if (handler.Callback(selected)) return;
    }
    if (!_Assets || !_Project || selected.IsSubAsset) return;
    const auto* meta=_Assets->QueryAsset(Guid(selected.GuidString));
    if (!meta) return;
    if (meta->_ImporterType==eAssetImporterType::Folder) {NavigateToDirectory(meta->GetPath());return;}
    const QDir assets(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));
    const auto file=QUrl::fromLocalFile(assets.filePath(QString::fromStdString(meta->GetPath())));
    if (!QDesktopServices::openUrl(file) && _Ui) _Ui->statusLabel->setText(QStringLiteral("系统无法打开该资源。"));
}

// 只记录鼠标/键盘右键上下文，不接管弹出逻辑和输入控件的原生菜单。
class ProjectAssetsContextCapture final : public QObject {
public:
    using Callback = std::function<void(QTreeView*, const QModelIndex&)>;
    using Shortcut = std::function<bool(QTreeView*,QKeyEvent*)>;
    ProjectAssetsContextCapture(QTreeView* tree, QObject* owner, Callback callback,Shortcut shortcut)
        : QObject(owner), _Tree(tree), _Callback(std::move(callback)),_Shortcut(std::move(shortcut)) {
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
            if(_Shortcut&&_Shortcut(_Tree,key))return true;
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
    Shortcut _Shortcut;
};

void ProjectAssetsUI::InstallContextCapture() {
    const auto remember = [this](QTreeView* tree, const QModelIndex& index) { RememberContextDirectory(tree,index); };
    const auto shortcut=[this](QTreeView* tree,QKeyEvent* key){return RouteAssetShortcut(tree,key);};
    new ProjectAssetsContextCapture(_Ui->folderTree, _Dispatch, remember,shortcut);
    new ProjectAssetsContextCapture(_Ui->assetTree, _Dispatch, remember,shortcut);
}

void ProjectAssetsUI::RememberContextDirectory(QTreeView*, const QModelIndex& index) {
    const QModelIndex first = index.siblingAtColumn(0);
    _ContextDirectory = first.isValid() && first.data(ProjectAssetsModel::FolderRole).toBool()
        ? first.data(ProjectAssetsModel::PathRole).toString() : _CurrentPath;
    _HasContextDirectory = true;
    _ContextAsset={};
    if (first.isValid()) {
        _ContextAsset.GuidString=first.data(ProjectAssetsModel::GuidRole).toString().toStdString();
        _ContextAsset.LocalID=first.data(ProjectAssetsModel::LocalIDRole).toULongLong();
        _ContextAsset.IsSubAsset=first.data(ProjectAssetsModel::SubAssetRole).toBool();
    }
    RefreshContextActions();
}

void ProjectAssetsUI::SelectPendingCreatedAsset(const std::vector<IAssetMeta*>& metas) {
    if (_PendingCreatedAssetPath.isEmpty()) return;
    // 导入完成时，用户可能已转去检查场景对象；自动选择不能覆盖新的主动选择。
    if (_Selection && _Selection->GetSceneObjectSelection().ObjectID) {
        _PendingCreatedAssetPath.clear();
        return;
    }
    for (IAssetMeta* meta:metas) {
        if (QString::fromStdString(meta->GetPath())!=_PendingCreatedAssetPath) continue;
        const QString parent=QFileInfo(_PendingCreatedAssetPath).path();
        NavigateToDirectory(parent=="." ? "" : parent.toStdString());
        _FolderSelected=false;
        _ExternalSelection={{meta->_Guid.GetGuidString(),0,false}};
        _RestoreExternalSelection=true;
        _PendingCreatedAssetPath.clear();
        return;
    }
}

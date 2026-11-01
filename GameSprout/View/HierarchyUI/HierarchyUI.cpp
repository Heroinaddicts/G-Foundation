#include "HierarchyUI.h"
#include "HierarchyTreeModel.h"
#include "ui_HierarchyUI.h"
#include <QApplication>
#include <QAction>
#include <QItemSelectionModel>
#include <QScrollBar>
#include <QSortFilterProxyModel>

HierarchyUI::~HierarchyUI() { Release(nullptr); }

void HierarchyUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) {
    g_Self = this;
    g_GameSproutEditor = editor;
    if (editor) {
        GetGameSproutView(editor, g_IIDEWindow, IDEWindow);
        GetGameSproutModule(editor, _SceneManager, SceneManager);
        GetGameSproutModule(editor, _Selection, SelectionManager);
    }
}

bool HierarchyUI::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    (void)editor;
    if (_Window) return true;
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) return false;
    try {
        _Dispatch = new QObject();
        _Window = new QWidget();
        _Ui = new Ui::HierarchyUIWindow();
        _Ui->setupUi(_Window);
        _Model = new HierarchyTreeModel(_Window);
        _Proxy = new QSortFilterProxyModel(_Window);
        _Proxy->setSourceModel(_Model);
        _Proxy->setFilterKeyColumn(0);
        _Proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        _Proxy->setRecursiveFilteringEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        _Proxy->setAutoAcceptChildRows(true);
#endif
        _Ui->hierarchyTree->setModel(_Proxy);
        QObject::connect(_Ui->hierarchyTree, &QTreeView::doubleClicked, _Dispatch,
            [this](const QModelIndex& index) { FocusDoubleClickedNode(index); });
        QObject::connect(_Window, &QObject::destroyed, _Dispatch, [this]() {
            _Window = nullptr; _Model = nullptr; _Proxy = nullptr; _Docker = nullptr; _DeleteMenu=nullptr; _Attached = false;
        });
        QObject::connect(_Ui->searchEdit, &QLineEdit::textChanged, _Dispatch, [this]() { _RefreshPending = true; });
        QObject::connect(_Ui->hierarchyTree, &QTreeView::expanded, _Dispatch, [this](const QModelIndex& index) {
            if (!_Rebuilding) _ExpandedIDs.insert(index.data(HierarchyTreeModel::NodeIDRole).toULongLong());
        });
        QObject::connect(_Ui->hierarchyTree, &QTreeView::collapsed, _Dispatch, [this](const QModelIndex& index) {
            if (!_Rebuilding) _ExpandedIDs.erase(index.data(HierarchyTreeModel::NodeIDRole).toULongLong());
        });
        QObject::connect(_Ui->hierarchyTree->selectionModel(), &QItemSelectionModel::currentChanged, _Dispatch,
            [this](const QModelIndex& index, const QModelIndex&) {
                if (_Rebuilding) return;
                _SelectedID = index.isValid() ? index.data(HierarchyTreeModel::NodeIDRole).toULongLong() : 0;
                PublishSelection();
                UpdateStatus();
            });
        QObject::connect(_Ui->hierarchyTree->selectionModel(), &QItemSelectionModel::selectionChanged, _Dispatch,
            [this]() {
                if (_Rebuilding) return;
                const QModelIndexList rows = _Ui->hierarchyTree->selectionModel()->selectedRows();
                _SelectedID = rows.empty() ? 0 : rows.front().data(HierarchyTreeModel::NodeIDRole).toULongLong();
                PublishSelection();
                UpdateStatus();
            });
        InstallContextCapture();
    } catch (...) { Release(nullptr); throw; }
    return true;
}

bool HierarchyUI::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    (void)editor;
    if (!_Window || !g_IIDEWindow) return false;
    _Docker = g_IIDEWindow->CreateDocker(QStringLiteral("Hierarchy"));
    if (!_Docker || !_Docker->AttachWidget(_Window)) return false;
    _Attached = true;
    _Docker->SetTitle(QStringLiteral("Hierarchy"));
    if (IIDEMenu* menu = _Docker->CreateChildMenu(_Ui->actionRefresh->property("panelText").toString())) menu->SetClickCallback(&HierarchyUI::RefreshMenu);
    RegisterContextMenus();
    // Docker 刚为内容安装了弹出过滤器；重新安装上下文捕获，使真实目标先被记录。
    InstallContextCapture();
    if (_SceneManager) {
        _SceneSubscribed = _SceneManager->RegisterSceneListener(this);
        if (!_SceneSubscribed || !BindRuntime()) return false;
    }
    if (_Selection) {
        _SelectionSubscribed = _Selection->RegisterSelectionListener(this);
        if (!_SelectionSubscribed) return false;
    }
    // 先订阅，再读取当前场景，避免启动与打开场景时丢失树变化。
    Refresh();
    return true;
}

void HierarchyUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
    (void)editor;
}

void HierarchyUI::Show() {
    if (_Window) _Window->show();
    if (_Docker) _Docker->Activate();
}

void HierarchyUI::Hide() {
    if (_Window) _Window->hide();
}

void HierarchyUI::Update(GameSproutEditor::GameSproutViewApi * const editor) {
    (void)editor;
    if (_RefreshPending && _Window) Refresh();
}

void HierarchyUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
    (void)editor;
    if (_SceneSubscribed && _SceneManager) _SceneManager->UnregisterSceneListener(this);
    if (_RuntimeSubscribed && _Runtime) _Runtime->UnregisterListener(this);
    if (_SelectionSubscribed && _Selection) _Selection->UnregisterSelectionListener(this);
    _SceneSubscribed = false; _RuntimeSubscribed = false; _SelectionSubscribed = false;
    _Runtime = nullptr; _SceneManager = nullptr; _Selection = nullptr;
    delete _Dispatch; _Dispatch = nullptr;
    _ContextCapture = nullptr;
    if (!_Attached) delete _Window;
    _Window = nullptr; _Docker = nullptr; _DeleteMenu=nullptr; _Attached = false;
    _Model = nullptr; _Proxy = nullptr;
    delete _Ui; _Ui = nullptr;
    _SceneID = 0; _SelectedID = 0; _ExpandedIDs.clear();
    _ContextSceneID = 0; _ContextObjectID = 0; _ContextCaptured = false;
    _RefreshPending = true; _Rebuilding = false;
    if (g_Self == this) g_Self = nullptr;
}

bool HierarchyUI::BindRuntime() {
    GameSproutRuntime::IGameSproutRuntime* runtime = g_GameSproutEditor ? g_GameSproutEditor->_Runtime : nullptr;
    if (runtime == _Runtime && (!runtime || _RuntimeSubscribed)) return true;
    if (_RuntimeSubscribed && _Runtime) _Runtime->UnregisterListener(this);
    _RuntimeSubscribed = false;
    _Runtime = runtime;
    if (_Runtime) {
        _RuntimeSubscribed = _Runtime->RegisterListener(this);
        return _RuntimeSubscribed;
    }
    return true;
}

void HierarchyUI::OnActiveSceneChanged() {
    _SceneID = 0; _SelectedID = 0; _ExpandedIDs.clear();
    _ContextSceneID = 0; _ContextObjectID = 0; _ContextCaptured = false;
    _RefreshPending = true;
}

void HierarchyUI::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    if (event.SceneID == _SceneID || event.ObjectID == _SceneID) _RefreshPending = true;
}

void HierarchyUI::RefreshMenu() { if (g_Self) g_Self->_RefreshPending = true; }

void HierarchyUI::Refresh() {
    if (!_Window || !_Model || !_Ui) return;
    if (!BindRuntime()) return;
    GameSproutRuntime::IScene* scene = _SceneManager ? _SceneManager->GetActiveScene() : nullptr;
    const std::uint64_t sceneID = scene ? scene->GetInstanceID() : 0;
    if (sceneID != _SceneID) {
        _SelectedID = 0; _ExpandedIDs.clear();
        if (sceneID) _ExpandedIDs.insert(sceneID);
    }
    _SceneID = sceneID;
    ReadSharedSelection();
    const int scroll = _Ui->hierarchyTree->verticalScrollBar()->value();
    _Rebuilding = true;
    struct ResetRebuilding { bool& Flag; ~ResetRebuilding() { Flag = false; } } reset{_Rebuilding};
    _Model->Build(scene, _Window->property("inactiveColor").value<QColor>());
    if (scene && _SceneManager) {
        QStandardItem* root = _Model->itemFromIndex(_Model->FindNode(sceneID));
        if (root) {
            const QString name = QString::fromStdString(scene->GetName());
            root->setText(name + (_SceneManager->HasUnsavedChanges() ? QStringLiteral(" *") : QString{}));
            const std::string& path = _SceneManager->GetActiveScenePath();
            root->setToolTip(path.empty() ? QStringLiteral("未保存的场景") : QStringLiteral("Assets/") + QString::fromStdString(path));
        }
    }
    _Proxy->setFilterFixedString(_Ui->searchEdit->text());
    if (_SelectedID && !_Model->FindNode(_SelectedID).isValid()) _SelectedID = 0;
    for (auto it = _ExpandedIDs.begin(); it != _ExpandedIDs.end();) {
        const QModelIndex source = _Model->FindNode(*it);
        if (!source.isValid()) { it = _ExpandedIDs.erase(it); continue; }
        _Ui->hierarchyTree->expand(_Proxy->mapFromSource(source));
        ++it;
    }
    if (!_Ui->searchEdit->text().isEmpty()) _Ui->hierarchyTree->expandAll();
    RestoreVisibleSelection();
    _Ui->hierarchyTree->doItemsLayout();
    _Ui->hierarchyTree->verticalScrollBar()->setValue(scroll);
    _RefreshPending = false;
    UpdateStatus();
}

void HierarchyUI::UpdateStatus() {
    if (!_Window || !_Ui || !_Model) return;
    if (!_SceneManager) _Ui->statusLabel->setText(QStringLiteral("场景服务尚未就绪"));
    else if (!_SceneID) _Ui->statusLabel->setText(QStringLiteral("当前没有打开的场景"));
    else {
        const std::size_t count = _Model->GetNodeCount();
        _Ui->statusLabel->setText(QStringLiteral("%1 个对象%2").arg(static_cast<qulonglong>(count ? count - 1 : 0))
            .arg(_SelectedID ? QStringLiteral(" · 已选择节点") : QString{}));
    }
}

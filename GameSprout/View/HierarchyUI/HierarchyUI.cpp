#include "HierarchyUI.h"
#include "HierarchyTreeModel.h"
#include "IScriptEditor.h"
#include "../Shared/SceneObjectDragPayload.h"
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
        _PlayMode=editor->GetGameSproutModule<IPlayMode>("PlayMode");
        _Scripts=editor->GetGameSproutModule<IScriptEditor>("ScriptEditor");
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
        _Model->SetDragSource([this](std::uint64_t worldID,std::uint64_t objectID)->QMimeData* {
            ScriptObjectTarget source;std::string error;
            if(!_Scripts || !_DragSessionID || !_Scripts->ReadObjectTarget(objectID,source,error) ||
               source.SessionID!=_DragSessionID || source.WorldID!=worldID)
                return nullptr;
            return SceneObjectDragPayload::Encode({source.SessionID,source.WorldID,source.ObjectID});
        });
        _Proxy = new QSortFilterProxyModel(_Window);
        _Proxy->setSourceModel(_Model);
        _Proxy->setFilterKeyColumn(0);
        _Proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        _Proxy->setRecursiveFilteringEnabled(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        _Proxy->setAutoAcceptChildRows(true);
#endif
        _Ui->hierarchyTree->setModel(_Proxy);
        _Ui->hierarchyTree->ClickCommitted=[this] {
            if(_Rebuilding)return;
            const auto rows=_Ui->hierarchyTree->selectionModel()->selectedRows();
            _SelectedID=rows.empty()?0:rows.front().data(HierarchyTreeModel::NodeIDRole).toULongLong();
            PublishSelection();UpdateStatus();
        };
        _Ui->hierarchyTree->PointerSelectionFinished=[this] {QueueRefresh(false);};
        QObject::connect(_Ui->hierarchyTree, &QTreeView::doubleClicked, _Dispatch,
            [this](const QModelIndex& index) { FocusDoubleClickedNode(index); });
        QObject::connect(_Window, &QObject::destroyed, _Dispatch, [this]() {
            _Window = nullptr; _Model = nullptr; _Proxy = nullptr; _Docker = nullptr; _DeleteMenu=nullptr; _Attached = false;
            _HeaderWidgets.clear();
            for(auto& action:_ExtensionActions)action.Menu=nullptr;
        });
        QObject::connect(_Ui->searchEdit, &QLineEdit::textChanged, _Dispatch, [this]() { QueueRefresh(false); });
        QObject::connect(_Ui->hierarchyTree, &QTreeView::expanded, _Dispatch, [this](const QModelIndex& index) {
            if (!_Rebuilding) _ExpandedIDs.insert(index.data(HierarchyTreeModel::NodeIDRole).toULongLong());
        });
        QObject::connect(_Ui->hierarchyTree, &QTreeView::collapsed, _Dispatch, [this](const QModelIndex& index) {
            if (!_Rebuilding) _ExpandedIDs.erase(index.data(HierarchyTreeModel::NodeIDRole).toULongLong());
        });
        QObject::connect(_Ui->hierarchyTree->selectionModel(), &QItemSelectionModel::currentChanged, _Dispatch,
            [this](const QModelIndex& index, const QModelIndex&) {
                if (_Rebuilding || _Ui->hierarchyTree->IsPointerSelecting()) return;
                _SelectedID = index.isValid() ? index.data(HierarchyTreeModel::NodeIDRole).toULongLong() : 0;
                PublishSelection();
                UpdateStatus();
            });
        QObject::connect(_Ui->hierarchyTree->selectionModel(), &QItemSelectionModel::selectionChanged, _Dispatch,
            [this]() {
                if (_Rebuilding || _Ui->hierarchyTree->IsPointerSelecting()) return;
                const QModelIndexList rows = _Ui->hierarchyTree->selectionModel()->selectedRows();
                _SelectedID = rows.empty() ? 0 : rows.front().data(HierarchyTreeModel::NodeIDRole).toULongLong();
                PublishSelection();
                UpdateStatus();
            });
        InstallContextCapture();
        InstallDropRouting();
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
    RegisterExtensionMenus();
    if(_PlayMode){_PlaySubscribed=_PlayMode->RegisterPlayModeListener(this);OnPlayModeStateChanged(_PlayMode->GetState());}
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
    // 先订阅；完整启动结束后读取一次初始树，后续由事件驱动。
    return true;
}

void HierarchyUI::LaunchFinished(GameSproutEditor::GameSproutViewApi * const) { _Launched=true;Refresh(); }

void HierarchyUI::Show() {
    if (_Window) _Window->show();
    if (_Docker) _Docker->Activate();
}

void HierarchyUI::Hide() {
    if (_Window) _Window->hide();
}

void HierarchyUI::Update(GameSproutEditor::GameSproutViewApi * const) {}

void HierarchyUI::Release(GameSproutEditor::GameSproutViewApi * const editor) {
    (void)editor;
    if(_PlaySubscribed&&_PlayMode)_PlayMode->UnregisterPlayModeListener(this);
    _PlaySubscribed=false;_PlayMode=nullptr;_CanEdit=true;
    if (_SceneSubscribed && _SceneManager) _SceneManager->UnregisterSceneListener(this);
    if (_RuntimeSubscribed && _Runtime) _Runtime->UnregisterListener(this);
    if (_SelectionSubscribed && _Selection) _Selection->UnregisterSelectionListener(this);
    _SceneSubscribed = false; _RuntimeSubscribed = false; _SelectionSubscribed = false;
    _Runtime = nullptr; _SceneManager = nullptr; _Selection = nullptr;
    if(_Model)_Model->SetDragSource({});
    if(_Window&&_Ui) {
        _Ui->hierarchyTree->ClickCommitted={};
        _Ui->hierarchyTree->PointerSelectionFinished={};
    }
    _Scripts=nullptr;
    if(_Docker)for(const auto& menu:_CreateMenus)menu.first->SetClickCalback(nullptr);
    _CreateMenus.clear();
    if(_DeleteMenu)_DeleteMenu->SetClickCalback(nullptr);
    for(auto& action:_ExtensionActions)if(action.Menu)action.Menu->SetClickCalback(nullptr);
    _ExtensionActions.clear();_AssetDropHandlers.clear();
    delete _Dispatch; _Dispatch = nullptr;
    const auto headers = std::move(_HeaderWidgets);
    _HeaderWidgets.clear();
    for (const auto& header : headers) delete header.data();
    _ContextCapture = nullptr;
    if (!_Attached) delete _Window;
    _Window = nullptr; _Docker = nullptr; _DeleteMenu=nullptr; _Attached = false;
    _Model = nullptr; _Proxy = nullptr;
    delete _Ui; _Ui = nullptr;
    _SceneID = 0; _DragSessionID=0; _SelectedID = 0; _ExpandedIDs.clear();
    _ContextSceneID = 0; _ContextObjectID = 0; _ContextCaptured = false;
    _Launched=false;_RefreshQueued = false; _RebuildRequested = false; _Rebuilding = false;
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
    if(_Window&&_Ui)_Ui->hierarchyTree->CancelPointerSelection(false);
    _DragSessionID=0;
    _SceneID = 0; _SelectedID = 0; _ExpandedIDs.clear();
    _ContextSceneID = 0; _ContextObjectID = 0; _ContextCaptured = false;
    RefreshContextActions();
    QueueRefresh();
}

void HierarchyUI::OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) {
    if (event.SceneID != _SceneID && event.ObjectID != _SceneID) return;
    switch(event.Type) {
    case GameSproutRuntime::SceneNodeEventType::Create:
    case GameSproutRuntime::SceneNodeEventType::Destroy:
    case GameSproutRuntime::SceneNodeEventType::Reparent:
    case GameSproutRuntime::SceneNodeEventType::Rename:
    case GameSproutRuntime::SceneNodeEventType::ActiveChanged:
    case GameSproutRuntime::SceneNodeEventType::SiblingOrderChanged:
        QueueRefresh();break;
    default:break; // 组件/动画姿态改变不改变层级树，无需重建全部节点。
    }
}

void HierarchyUI::RefreshMenu() { if (g_Self) g_Self->QueueRefresh(); }

void HierarchyUI::QueueRefresh(bool rebuild) {
    _RebuildRequested=_RebuildRequested||rebuild;
    if(!_Dispatch||!_Window||!_Launched||_RefreshQueued)return;
    _RefreshQueued=true;
    QMetaObject::invokeMethod(_Dispatch,[this]{
        _RefreshQueued=false;
        if(!_Window)return;
        if(_RebuildRequested)Refresh();else RefreshPresentation();
    },Qt::QueuedConnection);
}

void HierarchyUI::RefreshPresentation() {
    if(!_Window||!_Ui||!_Model||!_Proxy)return;
    _Rebuilding=true;
    struct Guard {bool& Flag;~Guard(){Flag=false;}} guard{_Rebuilding};
    auto* scene=_SceneManager?_SceneManager->GetActiveScene():nullptr;
    if(scene&&scene->GetInstanceID()==_SceneID) {
        auto* content=_SceneManager->GetDocumentContentRoot();
        if(auto* root=content?_Model->itemFromIndex(_Model->FindNode(content->GetInstanceID())):nullptr) {
            root->setText(QString::fromStdString(content->GetName())+(_SceneManager->HasUnsavedChanges()?QStringLiteral(" *"):QString{}));
            const auto& path=_SceneManager->GetActiveScenePath();
            root->setToolTip(_SceneManager->IsDocumentSessionActive()?QStringLiteral("独立编辑文档"):
                path.empty()?QStringLiteral("未保存的场景"):QStringLiteral("Assets/")+QString::fromStdString(path));
        }
    }
    _Proxy->setFilterFixedString(_Ui->searchEdit->text());
    if(!_Ui->searchEdit->text().isEmpty())_Ui->hierarchyTree->expandAll();
    if(!_Ui->hierarchyTree->IsPointerSelecting())RestoreVisibleSelection();
    UpdateStatus();
}

void HierarchyUI::Refresh() {
    if (!_Window || !_Model || !_Ui) return;
    _RebuildRequested=false;
    if (!BindRuntime()) return;
    GameSproutRuntime::IScene* scene = _SceneManager ? _SceneManager->GetActiveScene() : nullptr;
    const std::uint64_t sceneID = scene ? scene->GetInstanceID() : 0;
    const bool sceneChanged=sceneID!=_SceneID;
    if (sceneChanged) {
        _SelectedID = 0; _ExpandedIDs.clear();
        if (sceneID) _ExpandedIDs.insert(sceneID);
    }
    _SceneID = sceneID;
    auto* content=_SceneManager?_SceneManager->GetDocumentContentRoot():scene;
    if(sceneChanged&&sceneID&&content)_ExpandedIDs.insert(content->GetInstanceID());
    _DragSessionID=0;
    if(_Scripts&&scene) {
        ScriptObjectTarget scope;std::string error;
        if(_Scripts->ReadObjectTarget(sceneID,scope,error)&&scope.WorldID==sceneID)
            _DragSessionID=scope.SessionID;
    }
    ReadSharedSelection();
    const int scroll = _Ui->hierarchyTree->verticalScrollBar()->value();
    _Rebuilding = true;
    struct ResetRebuilding { bool& Flag; ~ResetRebuilding() { Flag = false; } } reset{_Rebuilding};
    _Model->Build(scene, _Window->property("inactiveColor").value<QColor>(),content);
    if (scene && _SceneManager) {
        QStandardItem* root = content?_Model->itemFromIndex(_Model->FindNode(content->GetInstanceID())):nullptr;
        if (root) {
            const QString name = QString::fromStdString(content->GetName());
            root->setText(name + (_SceneManager->HasUnsavedChanges() ? QStringLiteral(" *") : QString{}));
            const std::string& path = _SceneManager->GetActiveScenePath();
            root->setToolTip(_SceneManager->IsDocumentSessionActive()?QStringLiteral("独立编辑文档"):
                path.empty()?QStringLiteral("未保存的场景"):QStringLiteral("Assets/")+QString::fromStdString(path));
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
    UpdateStatus();
}

void HierarchyUI::UpdateStatus() {
    if (!_Window || !_Ui || !_Model) return;
    if (!_SceneManager) _Ui->statusLabel->setText(QStringLiteral("场景服务尚未就绪"));
    else if (!_SceneID) _Ui->statusLabel->setText(QStringLiteral("当前没有打开的场景"));
    else {
        const std::size_t count = _Model->GetNodeCount();
        const bool syntheticRoot=_SceneManager->GetDocumentContentRoot()==_SceneManager->GetActiveScene();
        _Ui->statusLabel->setText(QStringLiteral("%1 个对象%2").arg(static_cast<qulonglong>(count-(syntheticRoot&&count?1:0)))
            .arg(_SelectedID ? QStringLiteral(" · 已选择节点") : QString{}));
    }
}

void HierarchyUI::OnPlayModeStateChanged(const PlayModeState& state) {
    _CanEdit=state.State==EditorPlayState::Edit;
    RefreshContextActions();
    if(!_Docker)return;
    for(const auto& menu:_CreateMenus)menu.first->SetClickCalback(_CanEdit?menu.second:nullptr);
    if(_DeleteMenu)_DeleteMenu->SetClickCalback(nullptr);
}
void HierarchyUI::RegisterCreateMenu(IContextMenu* menu,fMenuClickCallback callback) {
    _CreateMenus.emplace_back(menu,callback);menu->SetClickCalback(_CanEdit?callback:nullptr);
}

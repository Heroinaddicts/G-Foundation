#ifndef __HierarchyUI_h__
#define __HierarchyUI_h__

#include "Header.h"
#include "IPlayMode.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <array>
#include <QPointer>
#include <set>
#include <vector>
#include <utility>

QT_BEGIN_NAMESPACE
class QWidget;
class QObject;
class QModelIndex;
class QMimeData;
class QPoint;
class QSortFilterProxyModel;
namespace Ui {
class HierarchyUIWindow;
}
QT_END_NAMESPACE

class HierarchyTreeModel;
class IScriptEditor;

class HierarchyUI : public IHierarchyUI, public ISceneListener, public GameSproutRuntime::IRuntimeListener, public ISelectionListener, public IPlayModeListener {
public:
    ~HierarchyUI() override;
    HierarchyUI() = default;
    HierarchyUI(const HierarchyUI&) = delete;
    HierarchyUI& operator=(const HierarchyUI&) = delete;

    void OnPlayModeStateChanged(const PlayModeState&) override;
    void Show() override;
    void Hide() override;
    std::uint64_t GetSelectedObjectID() const override { return _SelectedID; }
    bool AttachHeaderWidget(QWidget*) override;
    bool SetHeaderWidgetVisible(QWidget*,bool) override;
    bool RemoveHeaderWidget(QWidget*) override;
    bool RegisterAssetDropHandler(const void*,const fAssetDropProbe&,const fAssetDropExecute&) override;
    bool RegisterContextAction(const void*,const std::vector<std::string>&,fContextActionCallback,const fContextActionEnabled& = {}) override;
    HierarchyActionContext GetActionContext() const override;
    void UnregisterExtensions(const void*) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override { QueueRefresh(false); }
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>& selection) override;
    void OnSceneObjectSelectionChanged(const SceneObjectSelection& selection) override;

private:
    IPlayMode* _PlayMode=nullptr;
    IScriptEditor* _Scripts=nullptr;
    bool _PlaySubscribed=false;
    bool _CanEdit=true;
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool BindRuntime();
    void QueueRefresh(bool rebuild = true);
    void Refresh();
    void RefreshPresentation();
    void UpdateStatus();
    void InstallContextCapture();
    void RefreshHeaderVisibility();
    void RememberContext(const QModelIndex& index);
    void RegisterContextMenus();
    void RegisterExtensionMenus();
    void RefreshContextActions();
    void InstallDropRouting();
    bool RouteAssetDrop(const QPoint&,const QMimeData*,bool execute,std::string& error);
    void RegisterCreateMenu(IContextMenu*,fMenuClickCallback);
    std::vector<std::pair<IContextMenu*,fMenuClickCallback>> _CreateMenus;
    struct ExtensionAction {const void* Owner;std::vector<std::string> Path;fContextActionCallback Callback;fContextActionEnabled Enabled;IContextMenu* Menu=nullptr;};
    struct AssetDropHandler {uint64_t ID;const void* Owner;fAssetDropProbe Probe;fAssetDropExecute Execute;};
    std::vector<ExtensionAction> _ExtensionActions;
    std::vector<AssetDropHandler> _AssetDropHandlers;
    uint64_t _NextExtensionID=0;
    void PublishSelection();
    void FocusDoubleClickedNode(const QModelIndex& index);
    void ReadSharedSelection();
    void RestoreVisibleSelection();
    enum class NodeKind { Empty, Camera, Mesh, Canvas, Image, UIEmpty, Count };
    void CreateNode(NodeKind kind, bool child);
    void DeleteNode(bool context);
    static void DeleteContextMenu();
    template<NodeKind Kind, bool Child> static void CreateNodeMenu();
    static void RefreshMenu();

    QWidget* _Window = nullptr;
    std::vector<QPointer<QWidget>> _HeaderWidgets;
    QObject* _Dispatch = nullptr;
    QObject* _ContextCapture = nullptr;
    Ui::HierarchyUIWindow* _Ui = nullptr;
    HierarchyTreeModel* _Model = nullptr;
    QSortFilterProxyModel* _Proxy = nullptr;
    ISceneManager* _SceneManager = nullptr;
    ISelectionManager* _Selection = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime = nullptr;
    IIDEDocker* _Docker = nullptr;
    IContextMenu* _DeleteMenu = nullptr;
    bool _Attached = false;
    bool _Launched = false;
    bool _SceneSubscribed = false;
    bool _RuntimeSubscribed = false;
    bool _SelectionSubscribed = false;
    bool _RefreshQueued = false;
    bool _RebuildRequested = false;
    bool _Rebuilding = false;
    std::uint64_t _SceneID = 0;
    std::uint64_t _DragSessionID = 0;
    std::uint64_t _SelectedID = 0;
    std::uint64_t _ContextSceneID = 0;
    std::uint64_t _ContextObjectID = 0;
    bool _ContextCaptured = false;
    std::array<std::string, static_cast<std::size_t>(NodeKind::Count)> _NodeNames;
    std::set<std::uint64_t> _ExpandedIDs;
};
#endif //__HierarchyUI_h__

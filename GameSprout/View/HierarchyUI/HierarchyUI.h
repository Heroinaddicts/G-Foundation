#ifndef __HierarchyUI_h__
#define __HierarchyUI_h__

#include "Header.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <array>
#include <set>

QT_BEGIN_NAMESPACE
class QWidget;
class QObject;
class QModelIndex;
class QSortFilterProxyModel;
namespace Ui {
class HierarchyUIWindow;
}
QT_END_NAMESPACE

class HierarchyTreeModel;

class HierarchyUI : public IHierarchyUI, public ISceneListener, public GameSproutRuntime::IRuntimeListener, public ISelectionListener {
public:
    ~HierarchyUI() override;
    HierarchyUI() = default;
    HierarchyUI(const HierarchyUI&) = delete;
    HierarchyUI& operator=(const HierarchyUI&) = delete;

    void Show() override;
    void Hide() override;
    std::uint64_t GetSelectedObjectID() const override { return _SelectedID; }
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override { _RefreshPending = true; }
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>& selection) override;
    void OnSceneObjectSelectionChanged(const SceneObjectSelection& selection) override;

private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * const editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * const editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * const editor) override;
    bool BindRuntime();
    void Refresh();
    void UpdateStatus();
    void InstallContextCapture();
    void RememberContext(const QModelIndex& index);
    void RegisterContextMenus();
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
    bool _SceneSubscribed = false;
    bool _RuntimeSubscribed = false;
    bool _SelectionSubscribed = false;
    bool _RefreshPending = true;
    bool _Rebuilding = false;
    std::uint64_t _SceneID = 0;
    std::uint64_t _SelectedID = 0;
    std::uint64_t _ContextSceneID = 0;
    std::uint64_t _ContextObjectID = 0;
    bool _ContextCaptured = false;
    std::array<std::string, static_cast<std::size_t>(NodeKind::Count)> _NodeNames;
    std::set<std::uint64_t> _ExpandedIDs;
};
#endif //__HierarchyUI_h__

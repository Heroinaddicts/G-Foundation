#ifndef __ProjectAssetsUI_h__
#define __ProjectAssetsUI_h__
#include "Header.h"
#include "IAssetsManager.h"
#include "GameSproutRuntimeApi.h"
#include <set>
#include <QString>
class QWidget;
class QObject;
class ProjectAssetsModel;
class IProject;
class QModelIndex;
class QTreeView;
class QPoint;
class QMimeData;
class QKeyEvent;
namespace Ui { class ProjectAssetsUIWindow; }

class ProjectAssetsUI : public IProjectAssetsUI, public IAssetListener, public ISelectionListener {
public:
    ProjectAssetsUI() = default;
    ProjectAssetsUI(const ProjectAssetsUI &) = delete;
    ProjectAssetsUI & operator=(const ProjectAssetsUI &) = delete;
    ~ProjectAssetsUI() override;
    void Show() override;
    void Hide() override;
    std::vector<ProjectAssetSelection> GetSelection() const override;
    bool NavigateToDirectory(const std::string & path) override;
    bool RegisterAssetOpenHandler(const void* owner, const fAssetOpenHandler& handler) override;
    bool RegisterDropHandler(const void* owner,const fDropProbe& probe,const fDropExecute& execute) override;
    bool RegisterContextAction(const void* owner, const std::vector<std::string>& menuPath,
        fContextActionCallback callback, const fContextActionEnabled& enabled = {}) override;
    bool RegisterAssetShortcut(const void*,const std::string&,fContextActionCallback,const fContextActionEnabled& = {}) override;
    void UnregisterExtensions(const void* owner) override;
    ProjectAssetActionContext GetActionContext() const override;
    void NotifyAssetCreated(const std::string& relativePath) override;
    void OnAssetCreate(const AssetEvent &) override { QueueRefresh(); }
    void OnAssetModify(const AssetEvent &) override { QueueRefresh(); }
    void OnAssetRemove(const AssetEvent &) override { QueueRefresh(); }
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> & selection) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * editor) override;
    void QueueRefresh();
    void Refresh();
    void UpdateStatus();
    void PublishSelection();
    void RegisterContextMenus();
    void InstallContextCapture();
    void InstallDropRouting();
    bool RouteSceneObjectDrop(QTreeView*,const QPoint&,const QMimeData*,bool execute,std::string& error);
    void RememberContextDirectory(QTreeView* tree, const QModelIndex& index);
    void OpenAsset(const QModelIndex& index);
    void OpenAsset(const ProjectAssetSelection& selected);
    void RegisterExtensionMenus();
    void RefreshContextActions();
    void SelectPendingCreatedAsset(const std::vector<IAssetMeta*>& metas);
    bool RouteAssetShortcut(QTreeView*,QKeyEvent*);
    static void RefreshMenu();
    static void OpenAssetMenu();
    QWidget * _Window = nullptr;
    QObject * _Dispatch = nullptr;
    Ui::ProjectAssetsUIWindow * _Ui = nullptr;
    ProjectAssetsModel * _FolderModel = nullptr;
    ProjectAssetsModel * _AssetModel = nullptr;
    IAssetsManager * _Assets = nullptr;
    ISelectionManager * _Selection = nullptr;
    IProject* _Project = nullptr;
    ProjectAssetSelection _ContextAsset;
    IIDEDocker * _Docker = nullptr;
    bool _Attached = false;
    bool _Launched = false;
    bool _Subscribed = false;
    bool _SelectionSubscribed = false;
    bool _FolderSelected = false;
    bool _RestoreExternalSelection = false;
    bool _SynchronizingSelection = false;
    std::vector<ProjectAssetSelection> _ExternalSelection;
    bool _RefreshQueued = false;
    bool _RefreshDeferred = false;
    bool _Rebuilding = false;
    bool _ResetSelection = false;
    QString _CurrentPath;
    QString _CurrentFolderGuid;
    QString _ContextDirectory;
    QString _PendingCreatedAssetPath;
    bool _HasContextDirectory = false;
    std::set<QString> _ExpandedFolders{"root"};
    std::set<QString> _ExpandedAssets;
    struct OpenHandler {
        uint64_t ID;
        const void* Owner;
        fAssetOpenHandler Callback;
    };
    struct ContextAction {
        const void* Owner;
        std::vector<std::string> Path;
        fContextActionCallback Callback;
        fContextActionEnabled Enabled;
        IContextMenu* Menu = nullptr; // Docker 借用；Release 后不再访问。
    };
    std::vector<OpenHandler> _OpenHandlers;
    struct DropHandler {uint64_t ID;const void* Owner;fDropProbe Probe;fDropExecute Execute;};
    std::vector<DropHandler> _DropHandlers;
    std::vector<ContextAction> _ContextActions;
    struct KeyboardAction {const void* Owner;std::string Sequence;fContextActionCallback Callback;fContextActionEnabled Enabled;};
    std::vector<KeyboardAction> _KeyboardActions;
    uint64_t _NextExtensionID = 0;
};
#endif

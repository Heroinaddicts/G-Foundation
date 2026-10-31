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
class ISceneManager;
class IRuntimeAssets;
class ILibraryManager;
class QDialog;
class QModelIndex;
class QTreeView;
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
    void OnAssetCreate(const AssetEvent &) override { _Dirty = true; }
    void OnAssetModify(const AssetEvent &) override { _Dirty = true; }
    void OnAssetRemove(const AssetEvent &) override { _Dirty = true; }
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> & selection) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi * editor) override;
    void Update(GameSproutEditor::GameSproutViewApi * editor) override;
    void Release(GameSproutEditor::GameSproutViewApi * editor) override;
    void Refresh();
    void UpdateStatus();
    void PublishSelection();
    void RegisterContextMenus();
    void InstallContextCapture();
    void RememberContextDirectory(QTreeView* tree, const QModelIndex& index);
    void ShowCreateSceneDialog();
    void OpenSceneAsset(const QModelIndex& index);
    void SelectPendingCreatedAsset(const std::vector<IAssetMeta*>& metas);
    static void RefreshMenu();
    static void CreateSceneMenu();
    static void CreateAnimatorControllerMenu();
    static void InstantiateModelMenu();
    static void CreateAvatarMaskMenu();
    void ShowCreateAvatarMaskDialog();
    void ShowCreateAnimatorControllerDialog();
    void InstantiateSelectedModel();
    bool ResolveModelReference(GameSproutRuntime::ResourceReference& reference, std::string& error) const;
    QWidget * _Window = nullptr;
    QObject * _Dispatch = nullptr;
    Ui::ProjectAssetsUIWindow * _Ui = nullptr;
    ProjectAssetsModel * _FolderModel = nullptr;
    ProjectAssetsModel * _AssetModel = nullptr;
    IAssetsManager * _Assets = nullptr;
    ISelectionManager * _Selection = nullptr;
    ISceneManager * _Scenes = nullptr;
    IRuntimeAssets* _RuntimeAssets=nullptr;
    ILibraryManager* _Library=nullptr;
    ProjectAssetSelection _ContextAsset;
    QDialog * _CreateSceneDialog = nullptr;
    IIDEDocker * _Docker = nullptr;
    bool _Attached = false;
    bool _Subscribed = false;
    bool _SelectionSubscribed = false;
    bool _FolderSelected = false;
    bool _RestoreExternalSelection = false;
    bool _SynchronizingSelection = false;
    std::vector<ProjectAssetSelection> _ExternalSelection;
    bool _Dirty = true;
    bool _Rebuilding = false;
    bool _ResetSelection = false;
    QString _CurrentPath;
    QString _CurrentFolderGuid;
    QString _ContextDirectory;
    QString _PendingCreatedAssetPath;
    bool _HasContextDirectory = false;
    std::set<QString> _ExpandedFolders{"root"};
    std::set<QString> _ExpandedAssets;
};
#endif

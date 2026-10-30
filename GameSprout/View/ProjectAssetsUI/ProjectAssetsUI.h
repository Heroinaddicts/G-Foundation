#ifndef __ProjectAssetsUI_h__
#define __ProjectAssetsUI_h__
#include "Header.h"
#include "IAssetsManager.h"
#include <set>
#include <QString>
class QWidget;
class QObject;
class ProjectAssetsModel;
namespace Ui { class ProjectAssetsUIWindow; }

class ProjectAssetsUI : public IProjectAssetsUI, public IAssetListener {
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
private:
    void BeforeInitialize(GameSprout::GameSproutEditor * editor) override;
    bool Initialize(GameSprout::GameSproutEditor * editor) override;
    bool Launch(GameSprout::GameSproutEditor * editor) override;
    void LaunchFinished(GameSprout::GameSproutEditor * editor) override;
    void Update(GameSprout::GameSproutEditor * editor) override;
    void Release(GameSprout::GameSproutEditor * editor) override;
    void Refresh();
    void UpdateStatus();
    static void RefreshMenu();
    QWidget * _Window = nullptr;
    QObject * _Dispatch = nullptr;
    Ui::ProjectAssetsUIWindow * _Ui = nullptr;
    ProjectAssetsModel * _FolderModel = nullptr;
    ProjectAssetsModel * _AssetModel = nullptr;
    IAssetsManager * _Assets = nullptr;
    IIDEDocker * _Docker = nullptr;
    bool _Attached = false;
    bool _Subscribed = false;
    bool _Dirty = true;
    bool _Rebuilding = false;
    bool _ResetSelection = false;
    QString _CurrentPath;
    QString _CurrentFolderGuid;
    std::set<QString> _ExpandedFolders{"root"};
    std::set<QString> _ExpandedAssets;
};
#endif

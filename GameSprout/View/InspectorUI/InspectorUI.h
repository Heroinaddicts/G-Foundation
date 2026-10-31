#pragma once
#include "Header.h"
#include "IAssetsManager.h"
#include "ISelectionManager.h"
#include "ILibraryManager.h"
#include "ISceneManager.h"
#include <map>
namespace Ui { class InspectorUIWindow; }
class QWidget;
class QObject;
class ImportSettingsInspector;
class AssetInfoInspector;
class GameObjectInspector;

class InspectorUI final : public IInspectorUI, public ISelectionListener, public IAssetListener,
                          public ISceneListener, public GameSproutRuntime::IRuntimeListener {
public:
    InspectorUI() = default;
    ~InspectorUI() override;
    InspectorUI(const InspectorUI &) = delete;
    InspectorUI & operator=(const InspectorUI &) = delete;
    void Show() override;
    void Hide() override;
    bool HasPendingChanges() const override;
    bool ApplyChanges() override;
    void RevertChanges() override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> &) override { _Dirty = true; }
    void OnSceneObjectSelectionChanged(const SceneObjectSelection &) override { _Dirty = true; }
    void OnActiveSceneChanged() override { _Dirty = true; }
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent & event) override;
    void OnAssetCreate(const AssetEvent &) override { _Dirty = true; }
    void OnAssetModify(const AssetEvent &) override { _Dirty = true; }
    void OnAssetRemove(const AssetEvent &) override { _Dirty = true; }
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi * editor) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi * editor) override;
    bool Launch(GameSproutEditor::GameSproutViewApi * editor) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi *) override { _Dirty = true; }
    void Update(GameSproutEditor::GameSproutViewApi *) override;
    void Release(GameSproutEditor::GameSproutViewApi *) override;
    void Refresh();
    bool SyncRuntimeSubscription();
    GameSproutRuntime::IGameObject * ResolveSceneObject(const SceneObjectSelection & selection) const;
    void RefreshSceneObject(GameSproutRuntime::IGameObject & object);
    void UpdateButtons();
    bool ResolvePendingChanges();
    void Clear(const QString & message);
    QWidget * _Window = nullptr;
    QObject * _Dispatch = nullptr;
    Ui::InspectorUIWindow * _Ui = nullptr;
    std::map<eAssetImporterType, ImportSettingsInspector *> _Panels; // Qt parent owns panels.
    ImportSettingsInspector * _ActivePanel = nullptr;
    AssetInfoInspector * _Info = nullptr;
    GameObjectInspector * _SceneInfo = nullptr;
    IIDEDocker * _Docker = nullptr;
    IAssetsManager * _Assets = nullptr;
    ISelectionManager * _Selection = nullptr;
    ILibraryManager * _Library = nullptr;
    ISceneManager * _Scenes = nullptr;
    GameSproutRuntime::IGameSproutRuntime * _Runtime = nullptr;
    bool _Attached = false;
    bool _AssetSubscribed = false;
    bool _SelectionSubscribed = false;
    bool _SceneSubscribed = false;
    bool _RuntimeSubscribed = false;
    bool _Dirty = true;
    bool _Editable = false;
    bool _ResolvingSelection = false;
    bool _LockedSceneMissing = false;
    std::vector<ProjectAssetSelection> _Inspected;
    SceneObjectSelection _InspectedScene; // 只保存身份，每次刷新重新查询，不持有节点/组件。
    std::string _BaselineSettings;
    QString _OperationError;
};

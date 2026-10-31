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
class MaterialInspector;
class AnimationClipPreviewInspector;
class AnimatorControllerDialog;
class AvatarMaskDialog;
class ModelRigDialog;
class AvatarInspector;
class AvatarMaskInspector;
class IRuntimeAssets;

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
    bool EditAnimatorController(const GameSproutRuntime::ResourceReference&) override;
    bool EditModelRig(const GameSproutRuntime::ResourceReference&) override;
    bool EditAvatarMask(const GameSproutRuntime::ResourceReference&) override;
    void RevertChanges() override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> &) override { _Dirty = true; }
    void OnSceneObjectSelectionChanged(const SceneObjectSelection &) override { _Dirty = true; }
    void OnActiveSceneChanged() override { _Dirty = true; }
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent & event) override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
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
    void RefreshMaterial(IAssetMeta * meta, uint64_t localID, bool changed);
    void BindAnimationImportSources(IAssetMeta&,ImportSettingsInspector*);
    void RefreshAnimationClip(IAssetMeta&,IAsset&);
    bool ApplyMaterialChanges();
    void RevertMaterialChanges();
    bool SyncRuntimeSubscription();
    GameSproutRuntime::IGameObject * ResolveSceneObject(const SceneObjectSelection & selection) const;
    void RefreshSceneObject(GameSproutRuntime::IGameObject & object);
    void UpdateButtons();
    void SetDebugMode(bool debug);
    void UpdatePresentation();
    bool ResolvePendingChanges();
    void Clear(const QString & message);
    QWidget * _Window = nullptr;
    QObject * _Dispatch = nullptr;
    Ui::InspectorUIWindow * _Ui = nullptr;
    std::map<eAssetImporterType, ImportSettingsInspector *> _Panels; // Qt parent owns panels.
    ImportSettingsInspector * _ActivePanel = nullptr;
    AssetInfoInspector * _Info = nullptr;
    GameObjectInspector * _SceneInfo = nullptr;
    MaterialInspector * _Material = nullptr;
    AnimationClipPreviewInspector* _ClipPreview=nullptr;
    AnimatorControllerDialog* _ControllerDialog=nullptr;
    AvatarMaskDialog* _MaskDialog=nullptr;
    ModelRigDialog* _RigDialog=nullptr;
    AvatarInspector* _AvatarInspector=nullptr;
    AvatarMaskInspector* _MaskInspector=nullptr;
    IRuntimeAssets* _RuntimeAssets=nullptr;
    IIDEDocker * _Docker = nullptr;
    IIDEMenu * _NormalModeMenu = nullptr;
    IIDEMenu * _DebugModeMenu = nullptr;
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
    bool _MaterialMode = false;
    bool _ResolvingSelection = false;
    bool _LockedSceneMissing = false;
    bool _DebugMode = false;
    std::vector<ProjectAssetSelection> _Inspected;
    SceneObjectSelection _InspectedScene; // 只保存身份，每次刷新重新查询，不持有节点/组件。
    std::string _BaselineSettings;
    std::string _BaselineMaterialSource;
    QString _OperationError;
};

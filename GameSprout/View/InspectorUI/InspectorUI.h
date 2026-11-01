#pragma once
#include "Header.h"
#include "InspectorPanel.h"
#include "IAssetsManager.h"
#include "ISelectionManager.h"
#include "ILibraryManager.h"
#include "ISceneManager.h"
#include <map>
namespace Ui { class InspectorUIWindow; }
class AssetInfoInspector;
class InspectorUI final : public IInspectorUI, public ISelectionListener, public IAssetListener,
                          public ISceneListener, public GameSproutRuntime::IRuntimeListener {
public:
    ~InspectorUI() override;
    void Show() override;
    void Hide() override;
    bool HasPendingChanges() const override;
    bool ApplyChanges() override;
    void RevertChanges() override;
    bool RegisterExtension(IInspectorExtension*) override;
    void UnregisterExtension(IInspectorExtension*) override;
    ComponentEditor* CreateComponentEditor(const std::type_info&, QWidget*) override;
    void OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>&) override { RequestRefresh(); }
    void OnSceneObjectSelectionChanged(const SceneObjectSelection&) override { RequestRefresh(); }
    void OnActiveSceneChanged() override { RequestRefresh(); }
    void OnSceneStateChanged() override { RequestRefresh(); }
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override;
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification&) override;
    void OnAssetCreate(const AssetEvent&) override { RequestRefresh(); }
    void OnAssetModify(const AssetEvent&) override { RequestRefresh(); }
    void OnAssetRemove(const AssetEvent&) override { RequestRefresh(); }
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void LaunchFinished(GameSproutEditor::GameSproutViewApi*) override;
    void Update(GameSproutEditor::GameSproutViewApi*) override {}
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void RequestRefresh();
    void Refresh();
    void UpdateButtons();
    void SetDebugMode(bool);
    void UpdatePresentation();
    bool ResolvePendingChanges();
    void Clear(const QString&);
    bool SceneTargetExists(const SceneObjectSelection&) const;
    QWidget* _Window = nullptr;
    QObject* _Dispatch = nullptr;
    Ui::InspectorUIWindow* _Ui = nullptr;
    AssetInfoInspector* _Info = nullptr;
    IIDEDocker* _Docker = nullptr;
    IIDEMenu* _NormalModeMenu = nullptr;
    IIDEMenu* _DebugModeMenu = nullptr;
    InspectorServices _Services;
    InspectorTarget _Target;
    std::vector<IInspectorExtension*> _Extensions;
    std::map<IInspectorExtension*, InspectorPanel*> _Panels;
    InspectorPanel* _ActivePanel = nullptr;
    IInspectorExtension* _ActiveExtension = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime = nullptr;
    bool _Attached = false;
    bool _Launched = false;
    bool _Subscribed = false;
    bool _RuntimeSubscribed = false;
    bool _SceneSubscribed = false;
    bool _RefreshQueued = false;
    bool _RefreshDeferred = false;
    bool _ResolvingSelection = false;
    bool _LockedSceneMissing = false;
    bool _DebugMode = false;
    QString _OperationError;
};

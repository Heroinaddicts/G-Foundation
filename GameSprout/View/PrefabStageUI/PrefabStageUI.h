#pragma once
#include "Header.h"
#include "IPrefabStage.h"
#include "IProjectAssetsUI.h"
#include "ISceneEditorUI.h"
#include "ISceneViewUI.h"
#include "IPlayMode.h"
#include "IInspectorUI.h"
#include <QPointer>
#include <functional>
class IIDEWindow;
class IHierarchyUI;
class IAssetsManager;
class QDialog;
class QObject;
class QWidget;
namespace Ui {class PrefabStageToolbarForm;class PrefabStageHierarchyHeaderForm;}

class PrefabStageUI final : public IPrefabStageUI,public IPrefabStageListener,public IPlayModeListener {
public:
    ~PrefabStageUI() override;
    void OpenPrefab(const std::string&) override;
    void ReturnToMainScene() override;
    void OnPrefabStageChanged(const PrefabStageState&) override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Update(GameSproutEditor::GameSproutViewApi*) override {}
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    bool OpenAsset(const ProjectAssetSelection&);
    void QueueNavigation(const std::function<void()>&);
    void OpenRequested(const std::string&);
    void OpenNow(const std::string&);
    void Save();
    void SaveNow();
    void RequestClose(const std::function<void()>& afterClose={});
    void CloseNow(const std::function<void()>& afterClose);
    void ShowError(const std::string&);
    void Refresh(const PrefabStageState&);
    void UpdateSourceName();
    IIDEWindow* _IDE=nullptr;
    IHierarchyUI* _Hierarchy=nullptr;
    IAssetsManager* _Assets=nullptr;
    IProjectAssetsUI* _Project=nullptr;
    ISceneEditorUI* _SceneEditor=nullptr;
    ISceneViewUI* _SceneView=nullptr;
    IPrefabStage* _Stage=nullptr;
    IPlayMode* _Play=nullptr;
    IInspectorUI* _Inspector=nullptr;
    QObject* _Dispatch=nullptr;
    QPointer<QWidget> _Toolbar;
    QPointer<QWidget> _HierarchyHeader;
    QPointer<QDialog> _CloseDialog,_ErrorDialog;
    Ui::PrefabStageToolbarForm* _Ui=nullptr;
    Ui::PrefabStageHierarchyHeaderForm* _HeaderUi=nullptr;
    bool _Attached=false,_StageSubscribed=false,_PlaySubscribed=false,_CommandsRegistered=false;
    bool _HeaderAttached=false;
    bool _StageActive=false,_HaveMainCamera=false,_CanEdit=true;
    bool _HaveMainInspector=false;
    InspectorViewState _MainInspector;
    uint64_t _StageSceneID=0;
    uint64_t _NavigationGeneration=0;
    SceneViewCameraState _MainCamera;
    std::string _DialogGuid;
    QString _SourceCaption;
};

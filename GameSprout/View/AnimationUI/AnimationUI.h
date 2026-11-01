#pragma once
#include "Header.h"
#include "InspectorPanel.h"
#include "IProjectAssetsUI.h"
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;
class IAnimationEditor;
class IInspectorUI;
class AnimatorControllerDialog;
class AvatarMaskDialog;
class ModelRigDialog;
class QObject;
class QMenu;
namespace Ui { class AnimationActionsForm; }

class AnimationUI final : public IAnimationUI, public IInspectorExtension {
public:
    ~AnimationUI() override;
    void Show() override;
    void Hide() override;
    bool EditAnimatorController(const GameSproutRuntime::ResourceReference&) override;
    bool EditModelRig(const GameSproutRuntime::ResourceReference&) override;
    bool EditAvatarMask(const GameSproutRuntime::ResourceReference&) override;
    IModelAnimationEditor* CreateModelAnimationEditor(QWidget*) override;
    int Priority() const override { return 100; }
    bool CanInspect(const InspectorServices&, const InspectorTarget&) const override;
    InspectorPanel* CreatePanel(const InspectorServices&, QWidget*) override;
    ComponentEditor* CreateComponentEditor(const std::type_info&, QWidget*) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    bool OpenAsset(const ProjectAssetSelection&);
    bool ResolveModelReference(const ProjectAssetActionContext&, GameSproutRuntime::ResourceReference&, std::string&) const;
    void CreateAnimatorController();
    void CreateAvatarMask();
    static void CreateAnimatorControllerMenu();
    static void CreateAvatarMaskMenu();
    Ui::AnimationActionsForm* _Ui = nullptr;
    QMenu* _Actions = nullptr;
    QObject* _Dispatch = nullptr;
    IInspectorUI* _Inspector = nullptr;
    IProjectAssetsUI* _ProjectAssets = nullptr;
    IAssetsManager* _Assets = nullptr;
    ILibraryManager* _Library = nullptr;
    ISelectionManager* _Selection = nullptr;
    IAnimationEditor* _Authoring = nullptr;
    AnimatorControllerDialog* _ControllerDialog = nullptr;
    AvatarMaskDialog* _MaskDialog = nullptr;
    ModelRigDialog* _RigDialog = nullptr;
    bool _Registered = false;
};

#pragma once
#include "Header.h"
#include <QPointer>
class QDialog;
class QMenu;
namespace Ui {class PrefabActionsForm;}

// Owns Prefab authoring routes and additive Inspector UI, never a Docker.
class PrefabUI : public IPrefabUI, public IInspectorExtension, public ISceneListener, public IPlayModeListener {
public:
    ~PrefabUI() override;
    bool CanInspect(const InspectorServices&,const InspectorTarget&) const override;
    InspectorPanel* CreatePanel(const InspectorServices&,QWidget*) override;
    InspectorSection* CreateSceneObjectSection(const InspectorServices&,QWidget*) override;
    void OnActiveSceneChanged() override;
    void OnPlaySessionChanging(bool) override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Update(GameSproutEditor::GameSproutViewApi*) override {}
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    bool CanAuthor() const;
    bool ValidSceneTarget(uint64_t sceneID,uint64_t objectID,bool allowRoot=false) const;
    bool ResolvePrefab(const ProjectAssetSelection&,GameSproutRuntime::ResourceReference&) const;
    bool Instantiate(const ProjectAssetSelection&,uint64_t sceneID,uint64_t parentID,std::string& error);
    bool CanDropObject(const SceneObjectDropContext&) const;
    bool DropObject(const SceneObjectDropContext&,std::string&);
    bool CanDropAsset(const HierarchyAssetDropContext&) const;
    bool DropAsset(const HierarchyAssetDropContext&,std::string&);
    void SaveObject(const HierarchyActionContext&,const std::string& directory);
    void Message(const QString&);
    static void SaveMenu();
    static void InstantiateMenu();
    IInspectorUI* _Inspector=nullptr;
    IHierarchyUI* _Hierarchy=nullptr;
    IProjectAssetsUI* _ProjectAssets=nullptr;
    IPrefabManager* _Prefabs=nullptr;
    ISelectionManager* _Selection=nullptr;
    IAssetsManager* _Assets=nullptr;
    ILibraryManager* _Library=nullptr;
    ISceneManager* _Scenes=nullptr;
    IPlayMode* _Play=nullptr;
    IScriptEditor* _Scripts=nullptr;
    QPointer<QDialog> _SaveDialog,_MessageDialog;
    QMenu* _Actions=nullptr;
    Ui::PrefabActionsForm* _ActionsUi=nullptr;
    bool _Registered=false,_SceneSubscribed=false,_PlaySubscribed=false;
};

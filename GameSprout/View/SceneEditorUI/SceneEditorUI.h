#pragma once
#include "ISceneEditorUI.h"
#include "IPlayMode.h"
#include "ISceneManager.h"
#include "IIDEWindow.h"
#include "IProjectAssetsUI.h"
#include <functional>
#include <vector>
class QObject;
class QDialog;
class IAssetsManager;
class ILibraryManager;
class ISelectionManager;

class SceneEditorUI final : public ISceneEditorUI, public ISceneListener, public IPlayModeListener {
public:
    ~SceneEditorUI() override;
    void OnPlayModeStateChanged(const PlayModeState&) override;
    void SaveScene() override;
    void SaveSceneAs() override;
    void OpenSceneAsset(const std::string& path) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    bool RegisterDocumentHandler(const void*,const fDocumentActive&,const fDocumentSave&,const fDocumentClose&) override;
    void UnregisterDocumentHandler(const void*) override;
    void RefreshDocumentActions() override {RefreshActions();}
private:
    IPlayMode* _PlayMode=nullptr;
    bool _PlaySubscribed=false;
    bool _CanEdit=true;
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void ShowSaveDialog(const std::function<void()>& afterSave = {});
    void OpenNow(const std::string& path);
    void ShowError(const std::string& message);
    void RefreshActions();
    bool RegisterProjectActions();
    bool OpenProjectAsset(const ProjectAssetSelection& selected);
    bool ResolveModelReference(const ProjectAssetActionContext& context,
        GameSproutRuntime::ResourceReference& reference,std::string& error) const;
    void ShowCreateSceneDialog();
    void InstantiateSelectedModel();
    std::uint64_t SceneID() const;
    static void SaveMenu();
    static void SaveAsMenu();
    static void UndoMenu();
    static void RedoMenu();
    static void CreateSceneMenu();
    static void InstantiateModelMenu();
    inline static SceneEditorUI* _Self = nullptr;
    ISceneManager* _Scenes = nullptr;
    IProjectAssetsUI* _ProjectAssets = nullptr;
    IAssetsManager* _Assets = nullptr;
    ILibraryManager* _Library = nullptr;
    ISelectionManager* _Selection = nullptr;
    IIDEWindow* _IDE = nullptr;
    QObject* _Dispatch = nullptr;
    QDialog* _SaveDialog = nullptr;
    QDialog* _UnsavedDialog = nullptr;
    QDialog* _ErrorDialog = nullptr;
    QDialog* _CreateSceneDialog = nullptr;
    IIDEMenu* _Save = nullptr;
    IIDEMenu* _SaveAs = nullptr;
    IIDEMenu* _Undo = nullptr;
    IIDEMenu* _Redo = nullptr;
    bool _Subscribed = false;
    bool _Opening = false;
    struct DocumentHandler {
        uint64_t ID=0;
        const void* Owner=nullptr;
        fDocumentActive Active;
        fDocumentSave Save;
        fDocumentClose Close;
    };
    bool ReadDocumentHandler(DocumentHandler&) const;
    bool CloseActiveDocument(const std::function<void()>&);
    std::vector<DocumentHandler> _DocumentHandlers;
    uint64_t _NextDocumentHandler=0;
};

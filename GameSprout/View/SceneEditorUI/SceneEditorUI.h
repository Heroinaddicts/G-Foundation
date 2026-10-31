#pragma once
#include "ISceneEditorUI.h"
#include "ISceneManager.h"
#include "IIDEWindow.h"
#include <functional>
class QObject;
class QDialog;

class SceneEditorUI final : public ISceneEditorUI, public ISceneListener {
public:
    ~SceneEditorUI() override;
    void SaveScene() override;
    void SaveSceneAs() override;
    void OpenSceneAsset(const std::string& path) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) override;
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override;
    bool Launch(GameSproutEditor::GameSproutViewApi*) override;
    void Release(GameSproutEditor::GameSproutViewApi*) override;
    void ShowSaveDialog(const std::function<void()>& afterSave = {});
    void OpenNow(const std::string& path);
    void ShowError(const std::string& message);
    void RefreshActions();
    std::uint64_t SceneID() const;
    static void SaveMenu();
    static void SaveAsMenu();
    static void UndoMenu();
    static void RedoMenu();
    ISceneManager* _Scenes = nullptr;
    IIDEWindow* _IDE = nullptr;
    QObject* _Dispatch = nullptr;
    QDialog* _SaveDialog = nullptr;
    QDialog* _UnsavedDialog = nullptr;
    QDialog* _ErrorDialog = nullptr;
    IIDEMenu* _Save = nullptr;
    IIDEMenu* _SaveAs = nullptr;
    IIDEMenu* _Undo = nullptr;
    IIDEMenu* _Redo = nullptr;
    bool _Subscribed = false;
    bool _Opening = false;
};

#include "SceneEditorUI.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ISelectionManager.h"
#include "ui_SceneEditorActions.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QKeySequence>
#include <QMenu>

SceneEditorUI::~SceneEditorUI() { Release(nullptr); }
void SceneEditorUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) {
    _Self = this;
    _PlayMode=api?api->GetGameSproutModule<IPlayMode>("PlayMode"):nullptr;
    _IDE = api ? api->GetGameSproutView<IIDEWindow>("IDEWindow") : nullptr;
    _Scenes = api ? api->GetGameSproutModule<ISceneManager>("SceneManager") : nullptr;
    _ProjectAssets = api ? api->GetGameSproutView<IProjectAssetsUI>("ProjectAssetsUI") : nullptr;
    _Assets = api ? api->GetGameSproutModule<IAssetsManager>("AssetsManager") : nullptr;
    _Library = api ? api->GetGameSproutModule<ILibraryManager>("LibraryManager") : nullptr;
    _Selection = api ? api->GetGameSproutModule<ISelectionManager>("SelectionManager") : nullptr;
}
bool SceneEditorUI::Initialize(GameSproutEditor::GameSproutViewApi*) {
    if (!_Dispatch) _Dispatch = new QObject();
    return _IDE && _Scenes;
}
bool SceneEditorUI::Launch(GameSproutEditor::GameSproutViewApi*) {
    IIDEMenuBar* bar = _IDE ? _IDE->GetMenuBar() : nullptr;
    if (!bar) return false;
    QMenu definition;
    Ui::SceneEditorActions form;
    form.setupUi(&definition);
    for (QAction* group : definition.actions()) {
        if (!group->menu()) continue;
        IIDEMenu* parent = bar->CreateChildMenu(group->text());
        if (!parent) return false;
        for (QAction* action : group->menu()->actions()) {
            IIDEMenu* menu = parent->CreateChildMenu(action->text());
            if (!menu) return false;
            menu->SetShortcut(action->shortcut().toString(QKeySequence::PortableText));
            if (action->objectName() == "actionSaveScene") { _Save = menu; menu->SetClickCallback(&SaveMenu); }
            else if (action->objectName() == "actionSaveSceneAs") { _SaveAs = menu; menu->SetClickCallback(&SaveAsMenu); }
            else if (action->objectName() == "actionUndo") { _Undo = menu; menu->SetClickCallback(&UndoMenu); }
            else if (action->objectName() == "actionRedo") { _Redo = menu; menu->SetClickCallback(&RedoMenu); }
        }
    }
    if (!RegisterProjectActions()) return false;
    _Subscribed = _Scenes->RegisterSceneListener(this);
    if(_PlayMode){_PlaySubscribed=_PlayMode->RegisterPlayModeListener(this);OnPlayModeStateChanged(_PlayMode->GetState());}
    RefreshActions();
    return _Subscribed;
}
void SceneEditorUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if(_PlaySubscribed&&_PlayMode)_PlayMode->UnregisterPlayModeListener(this);
    _PlaySubscribed=false;_PlayMode=nullptr;_CanEdit=true;
    if (_ProjectAssets) _ProjectAssets->UnregisterExtensions(this);
    if (_Subscribed && _Scenes) _Scenes->UnregisterSceneListener(this);
    _Subscribed = false;
    _DocumentHandlers.clear();
    delete _SaveDialog; _SaveDialog = nullptr;
    delete _UnsavedDialog; _UnsavedDialog = nullptr;
    delete _ErrorDialog; _ErrorDialog = nullptr;
    delete _CreateSceneDialog; _CreateSceneDialog = nullptr;
    delete _Dispatch; _Dispatch = nullptr;
    // 菜单包装由 IDEWindow 管理，此处清除借用引用。
    _Save = nullptr; _SaveAs = nullptr; _Undo = nullptr; _Redo = nullptr;
    _Scenes = nullptr; _IDE = nullptr; _Opening = false;
    _ProjectAssets=nullptr;_Assets=nullptr;_Library=nullptr;_Selection=nullptr;
    if (_Self == this) _Self = nullptr;
}
std::uint64_t SceneEditorUI::SceneID() const {
    auto* scene = _Scenes ? _Scenes->GetActiveScene() : nullptr;
    return scene ? scene->GetInstanceID() : 0;
}
void SceneEditorUI::RefreshActions() {
    const bool editing = !_CanEdit || (_Scenes && _Scenes->HasTransformEdit());
    DocumentHandler document;const bool external=ReadDocumentHandler(document);
    if (_Save) _Save->SetEnabled(SceneID() != 0 && !editing);
    if (_SaveAs) _SaveAs->SetEnabled(SceneID() != 0 && !editing && !external);
    if (_Undo) _Undo->SetEnabled(_Scenes && !editing && _Scenes->CanUndo());
    if (_Redo) _Redo->SetEnabled(_Scenes && !editing && _Scenes->CanRedo());
}
void SceneEditorUI::OnActiveSceneChanged() {
    if (!_Opening) {
        if (_SaveDialog) _SaveDialog->reject();
        if (_UnsavedDialog) _UnsavedDialog->reject();
    }
    RefreshActions();
}
void SceneEditorUI::OnSceneStateChanged() { RefreshActions(); }
void SceneEditorUI::SaveMenu() { if (_Self) _Self->SaveScene(); }
void SceneEditorUI::SaveAsMenu() { if (_Self) _Self->SaveSceneAs(); }
void SceneEditorUI::UndoMenu() {
    if (!_Self || !_Self->_CanEdit || !_Self->_Scenes || _Self->_Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    std::string error;
    if (!_Self->_Scenes->Undo(error)) _Self->ShowError(error);
    _Self->RefreshActions();
}
void SceneEditorUI::RedoMenu() {
    if (!_Self || !_Self->_CanEdit || !_Self->_Scenes || _Self->_Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    std::string error;
    if (!_Self->_Scenes->Redo(error)) _Self->ShowError(error);
    _Self->RefreshActions();
}
void SceneEditorUI::SaveScene() {
    if (!_CanEdit || !_Scenes || !SceneID() || _Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    DocumentHandler document;
    if(ReadDocumentHandler(document)){document.Save();return;}
    if (_Scenes->GetActiveScenePath().empty()) { ShowSaveDialog(); return; }
    std::string error;
    if (!_Scenes->SaveScene(error)) ShowError(error);
}
void SceneEditorUI::SaveSceneAs() {
    if (!_CanEdit || !_Scenes || _Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    DocumentHandler document;if(ReadDocumentHandler(document))return;
    if (SceneID()) ShowSaveDialog();
}
void SceneEditorUI::OpenNow(const std::string& path) {
    std::string error;
    _Opening = true;
    const bool success = _Scenes && _Scenes->OpenScene(path, error);
    _Opening = false;
    if (!success) ShowError(error);
}

void SceneEditorUI::OnPlayModeStateChanged(const PlayModeState& state) {
    _CanEdit=state.State==EditorPlayState::Edit;
    if(!_CanEdit){if(_SaveDialog)_SaveDialog->reject();if(_UnsavedDialog)_UnsavedDialog->reject();if(_CreateSceneDialog)_CreateSceneDialog->reject();}
    RefreshActions();
}

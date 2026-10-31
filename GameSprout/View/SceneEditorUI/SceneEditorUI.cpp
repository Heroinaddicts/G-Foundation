#include "SceneEditorUI.h"
#include "ui_SceneEditorActions.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QKeySequence>
#include <QMenu>

namespace { SceneEditorUI* Instance = nullptr; }
SceneEditorUI::~SceneEditorUI() { Release(nullptr); }
void SceneEditorUI::BeforeInitialize(GameSproutEditor::GameSproutViewApi* api) {
    Instance = this;
    _IDE = api ? api->GetGameSproutView<IIDEWindow>("IDEWindow") : nullptr;
    _Scenes = api ? api->GetGameSproutModule<ISceneManager>("SceneManager") : nullptr;
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
    _Subscribed = _Scenes->RegisterSceneListener(this);
    RefreshActions();
    return _Subscribed;
}
void SceneEditorUI::Release(GameSproutEditor::GameSproutViewApi*) {
    if (_Subscribed && _Scenes) _Scenes->UnregisterSceneListener(this);
    _Subscribed = false;
    delete _SaveDialog; _SaveDialog = nullptr;
    delete _UnsavedDialog; _UnsavedDialog = nullptr;
    delete _ErrorDialog; _ErrorDialog = nullptr;
    delete _Dispatch; _Dispatch = nullptr;
    // IDEWindow 先 Release，不能在这里再解引用它管理的菜单包装对象。
    _Save = nullptr; _SaveAs = nullptr; _Undo = nullptr; _Redo = nullptr;
    _Scenes = nullptr; _IDE = nullptr; _Opening = false;
    if (Instance == this) Instance = nullptr;
}
std::uint64_t SceneEditorUI::SceneID() const {
    auto* scene = _Scenes ? _Scenes->GetActiveScene() : nullptr;
    return scene ? scene->GetInstanceID() : 0;
}
void SceneEditorUI::RefreshActions() {
    const bool editing = _Scenes && _Scenes->HasTransformEdit();
    if (_Save) _Save->SetEnabled(SceneID() != 0 && !editing);
    if (_SaveAs) _SaveAs->SetEnabled(SceneID() != 0 && !editing);
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
void SceneEditorUI::SaveMenu() { if (Instance) Instance->SaveScene(); }
void SceneEditorUI::SaveAsMenu() { if (Instance) Instance->SaveSceneAs(); }
void SceneEditorUI::UndoMenu() {
    if (!Instance || !Instance->_Scenes || Instance->_Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    std::string error;
    if (!Instance->_Scenes->Undo(error)) Instance->ShowError(error);
    Instance->RefreshActions();
}
void SceneEditorUI::RedoMenu() {
    if (!Instance || !Instance->_Scenes || Instance->_Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    std::string error;
    if (!Instance->_Scenes->Redo(error)) Instance->ShowError(error);
    Instance->RefreshActions();
}
void SceneEditorUI::SaveScene() {
    if (!_Scenes || !SceneID() || _Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    if (_Scenes->GetActiveScenePath().empty()) { ShowSaveDialog(); return; }
    std::string error;
    if (!_Scenes->SaveScene(error)) ShowError(error);
}
void SceneEditorUI::SaveSceneAs() {
    if (!_Scenes || _Scenes->HasTransformEdit()) return;
    if (QWidget* focused = QApplication::focusWidget()) focused->clearFocus();
    if (SceneID()) ShowSaveDialog();
}
void SceneEditorUI::OpenNow(const std::string& path) {
    std::string error;
    _Opening = true;
    const bool success = _Scenes && _Scenes->OpenScene(path, error);
    _Opening = false;
    if (!success) ShowError(error);
}

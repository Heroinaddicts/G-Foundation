#include "HierarchyUI.h"
#include "HierarchyTreeModel.h"
#include "ISceneViewUI.h"
#include "ui_HierarchyUI.h"
#include <QItemSelectionModel>
#include <QSortFilterProxyModel>

void HierarchyUI::FocusDoubleClickedNode(const QModelIndex& index) {
    if (_Rebuilding || !index.isValid() || !_SceneManager || !_Runtime || !_Selection) return;
    const std::uint64_t objectID = index.data(HierarchyTreeModel::NodeIDRole).toULongLong();
    GameSproutRuntime::IScene* scene = _SceneManager->GetActiveScene();
    GameSproutRuntime::IGameObject* object = _Runtime->QueryGameObject(objectID);
    // 场景标题不参与相机聚焦；只处理当前场景中仍有效的对象。
    if (!scene || scene->GetInstanceID() != _SceneID || !object || object == scene || object->GetScene() != scene) return;
    // 先同步选择，再复用 F 聚焦；不能依赖稍后 Update 才派发的选择通知。
    _SelectedID = objectID;
    PublishSelection();
    UpdateStatus();
    ISceneViewUI* view = g_GameSproutEditor ? g_GameSproutEditor->GetGameSproutView<ISceneViewUI>("SceneViewUI") : nullptr;
    if (view) {
        view->Show();
        view->FocusSelection();
    }
}

void HierarchyUI::PublishSelection() {
    if (!_Selection) return;
    SceneObjectSelection selection;
    GameSproutRuntime::IScene* scene = _SceneManager ? _SceneManager->GetActiveScene() : nullptr;
    GameSproutRuntime::IGameObject* object = _Runtime ? _Runtime->QueryGameObject(_SelectedID) : nullptr;
    if (scene && object && object->GetScene() == scene) selection = {scene->GetInstanceID(), _SelectedID};
    _Selection->SetSceneObjectSelection(selection);
}

void HierarchyUI::ReadSharedSelection() {
    if (!_Selection) return;
    const SceneObjectSelection selection = _Selection->GetSceneObjectSelection();
    GameSproutRuntime::IGameObject* object = _Runtime ? _Runtime->QueryGameObject(selection.ObjectID) : nullptr;
    GameSproutRuntime::IScene* scene = object ? object->GetScene() : nullptr;
    _SelectedID = _SceneID && selection.SceneID == _SceneID && scene &&
        scene->GetInstanceID() == _SceneID ? selection.ObjectID : 0;
}

void HierarchyUI::RestoreVisibleSelection() {
    if (!_Window || !_Ui || !_Model || !_Proxy) return;
    QItemSelectionModel* selection = _Ui->hierarchyTree->selectionModel();
    const QModelIndex index = _SelectedID ? _Proxy->mapFromSource(_Model->FindNode(_SelectedID)) : QModelIndex{};
    if (index.isValid()) selection->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    else {
        selection->clearSelection();
        selection->clearCurrentIndex();
    }
}

void HierarchyUI::OnSceneObjectSelectionChanged(const SceneObjectSelection&) {
    // 总是读取服务当前值；合并派发期间其他监听者可能已经改变了选择。
    ReadSharedSelection();
    if (!_Window) return;
    if (_SelectedID && _Runtime) {
        GameSproutRuntime::IGameObject* object = _Runtime->QueryGameObject(_SelectedID);
        for (GameSproutRuntime::IGameObject* parent = object ? object->Parent() : nullptr; parent; parent = parent->Parent())
            _ExpandedIDs.insert(parent->GetInstanceID());
    }
    _RefreshPending = true;
    const bool rebuilding = _Rebuilding;
    _Rebuilding = true;
    RestoreVisibleSelection();
    _Rebuilding = rebuilding;
    UpdateStatus();
}

void HierarchyUI::OnAssetSelectionChanged(const std::vector<ProjectAssetSelection>& selection) {
    if (!selection.empty()) OnSceneObjectSelectionChanged({});
}

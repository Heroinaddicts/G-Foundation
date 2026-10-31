#include "HierarchyUI.h"
#include "ui_HierarchyUI.h"
#include <QItemSelectionModel>
#include <QSortFilterProxyModel>
#include <limits>

void HierarchyUI::CreateNode(NodeKind kind, bool child) {
    if (!_Window || !_SceneManager || !BindRuntime() || !_Runtime || _Runtime->IsExecuting()) return;
    GameSproutRuntime::IScene* scene = _SceneManager->GetActiveScene();
    if (!scene) { _Ui->statusLabel->setText(QStringLiteral("当前没有打开的场景，无法创建节点。")); return; }
    const std::uint64_t contextScene = _ContextSceneID;
    const std::uint64_t contextObject = _ContextObjectID;
    const bool captured = _ContextCaptured;
    _ContextCaptured = false; _ContextSceneID = 0; _ContextObjectID = 0;
    GameSproutRuntime::IGameObject* parent = scene;
    if (child && captured) {
        if (contextScene != scene->GetInstanceID()) {
            _Ui->statusLabel->setText(QStringLiteral("场景已经切换，请重新右键选择父节点。")); return;
        }
        if (contextObject) {
            parent = _Runtime->QueryGameObject(contextObject);
            if (!parent || parent->GetScene() != scene) {
                _Ui->statusLabel->setText(QStringLiteral("右键目标已经失效，请重新选择父节点。")); return;
            }
        }
    }
    const std::size_t kindIndex = static_cast<std::size_t>(kind);
    if (kindIndex >= _NodeNames.size() || _NodeNames[kindIndex].empty()) return;
    const std::string baseName = _NodeNames[kindIndex];
    std::string name = baseName;
    for (std::uint64_t suffix = 1; parent->FindChild(name); ++suffix) {
        if (suffix == std::numeric_limits<std::uint64_t>::max()) return;
        name = baseName + " (" + std::to_string(suffix) + ")";
    }
    GameSproutRuntime::IGameObject* object = scene->CreateGameObject(name, parent);
    if (!object) { _Ui->statusLabel->setText(QStringLiteral("Runtime 无法创建节点。")); return; }
    bool configured = true;
    switch (kind) {
    case NodeKind::Empty: break;
    case NodeKind::Camera: configured = object->AddComponent<GameSproutRuntime::ICameraComponent>() != nullptr; break;
    case NodeKind::Mesh:
        configured = object->AddComponent<GameSproutRuntime::IMeshComponent>() && object->AddComponent<GameSproutRuntime::IMeshRendererComponent>(); break;
    case NodeKind::Canvas:
        configured = object->AddComponent<GameSproutRuntime::IRectTransformComponent>() && object->AddComponent<GameSproutRuntime::ICanvasComponent>(); break;
    case NodeKind::Image:
        configured = object->AddComponent<GameSproutRuntime::IRectTransformComponent>() && object->AddComponent<GameSproutRuntime::IImageComponent>(); break;
    case NodeKind::UIEmpty: configured = object->AddComponent<GameSproutRuntime::IRectTransformComponent>() != nullptr; break;
    case NodeKind::Count: configured = false; break;
    }
    if (!configured) {
        scene->DestroyGameObject(object);
        _Ui->statusLabel->setText(QStringLiteral("组件创建失败，已撤销新节点。"));
        _RefreshPending = true;
        return;
    }
    if (_SceneID != scene->GetInstanceID()) {
        _SceneID = scene->GetInstanceID();
        _ExpandedIDs.clear();
    }
    _SelectedID = object->GetInstanceID();
    for (GameSproutRuntime::IGameObject* ancestor = parent; ancestor; ancestor = ancestor->Parent())
        _ExpandedIDs.insert(ancestor->GetInstanceID());
    _Ui->searchEdit->clear();
    PublishSelection();
    _RefreshPending = true;
    Refresh();
    _Ui->hierarchyTree->scrollTo(_Ui->hierarchyTree->currentIndex());
    _Ui->hierarchyTree->setFocus();
}

#include "HierarchyTreeModel.h"
#include <vector>

QStandardItem* HierarchyTreeModel::MakeItem(GameSproutRuntime::IGameObject* object, bool sceneRoot, const QColor& inactiveColor) {
    const std::uint64_t id = object->GetInstanceID();
    if (!id || _Nodes.count(id)) return nullptr;
    auto* item = new QStandardItem(QString::fromStdString(object->GetName()));
    item->setEditable(false);
    item->setData(QVariant::fromValue<qulonglong>(id), NodeIDRole);
    const bool active = object->IsActiveInHierarchy();
    item->setData(active, ActiveRole);
    item->setData(sceneRoot, SceneRootRole);
    if (!active) item->setForeground(inactiveColor);
    item->setToolTip(QStringLiteral("%1\nID: %2").arg(item->text()).arg(id));
    try { _Nodes.emplace(id, item); }
    catch (...) { delete item; throw; }
    return item;
}

void HierarchyTreeModel::Build(GameSproutRuntime::IScene* scene, const QColor& inactiveColor) {
    _Nodes.clear();
    clear();
    setColumnCount(1);
    if (!scene) return;
    QStandardItem* root = MakeItem(scene, true, inactiveColor);
    if (!root) return;
    appendRow(root);
    // 使用显式栈，深层节点不递归消耗调用栈；Model 只保存节点 ID，不借用运行对象。
    struct Pending { GameSproutRuntime::IGameObject* Object; QStandardItem* Item; };
    std::vector<Pending> pending{{scene, root}};
    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        for (std::size_t index = 0; index < current.Object->GetChildCount(); ++index) {
            GameSproutRuntime::IGameObject* child = current.Object->GetChild(index);
            if (!child) continue;
            QStandardItem* item = MakeItem(child, false, inactiveColor);
            if (!item) continue;
            current.Item->appendRow(item);
            pending.push_back({child, item});
        }
    }
}

QModelIndex HierarchyTreeModel::FindNode(std::uint64_t id) const {
    const auto found = _Nodes.find(id);
    return found == _Nodes.end() ? QModelIndex{} : found->second->index();
}

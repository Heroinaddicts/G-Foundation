#include "HierarchyTreeModel.h"
#include "../Shared/SceneObjectDragPayload.h"
#include <algorithm>
#include <vector>

QStandardItem* HierarchyTreeModel::MakeItem(GameSproutRuntime::IGameObject* object, bool sceneRoot) {
    const std::uint64_t id = object->GetInstanceID();
    if (!id || _Nodes.count(id)) return nullptr;
    auto* item = new QStandardItem(QString::fromStdString(object->GetName()));
    item->setEditable(false);
    item->setDragEnabled(true);
    item->setData(QVariant::fromValue<qulonglong>(id), NodeIDRole);
    const bool active = object->IsActiveInHierarchy();
    item->setData(active, ActiveRole);
    item->setData(sceneRoot, SceneRootRole);
    item->setData(QVariant::fromValue<qulonglong>(_SceneID), SceneIDRole);
    if (!active) item->setForeground(_InactiveColor);
    item->setToolTip(QStringLiteral("%1\nID: %2").arg(item->text()).arg(id));
    try { _Nodes.emplace(id, item); }
    catch (...) { delete item; throw; }
    return item;
}

void HierarchyTreeModel::Build(GameSproutRuntime::IScene* scene, const QColor& inactiveColor,GameSproutRuntime::IGameObject* contentRoot) {
    _Nodes.clear();
    clear();
    setColumnCount(1);
    _SceneID=scene?scene->GetInstanceID():0;
    _RootID=0;
    _InactiveColor=inactiveColor;
    if (!scene) return;
    if(!contentRoot)contentRoot=scene;
    if(contentRoot->GetScene()!=scene)return;
    QStandardItem* root = MakeItem(contentRoot, contentRoot==scene);
    if (!root) return;
    _RootID=contentRoot->GetInstanceID();
    appendRow(root);
    BuildChildren(contentRoot, root);
}

void HierarchyTreeModel::BuildChildren(GameSproutRuntime::IGameObject* object, QStandardItem* item) {
    // 使用显式栈，深层节点不递归消耗调用栈；Model 只保存节点 ID，不借用运行对象。
    struct Pending { GameSproutRuntime::IGameObject* Object; QStandardItem* Item; };
    std::vector<Pending> pending{{object, item}};
    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        for (std::size_t index = 0; index < current.Object->GetChildCount(); ++index) {
            GameSproutRuntime::IGameObject* child = current.Object->GetChild(index);
            if (!child) continue;
            QStandardItem* item = MakeItem(child, false);
            if (!item) continue;
            current.Item->appendRow(item);
            pending.push_back({child, item});
        }
    }
}

void HierarchyTreeModel::RemoveNode(std::uint64_t id) {
    const auto found = _Nodes.find(id);
    if (found == _Nodes.end()) return;
    auto* item = found->second;
    std::vector<QStandardItem*> pending{item};
    while (!pending.empty()) {
        auto* child = pending.back();
        pending.pop_back();
        _Nodes.erase(child->data(NodeIDRole).toULongLong());
        for (int row = 0; row < child->rowCount(); ++row) pending.push_back(child->child(row));
    }
    if (auto* parent = item->parent()) parent->removeRow(item->row());
    else removeRow(item->row());
}

void HierarchyTreeModel::SyncStructure(GameSproutRuntime::IGameSproutRuntime& runtime,
                                      const std::set<std::uint64_t>& changed) {
    if (!_RootID || changed.empty()) return;
    struct Parent { GameSproutRuntime::IGameObject* Object; std::size_t Depth; };
    std::map<std::uint64_t, Parent> parents;
    for (const auto id : changed) {
        if (id == _RootID) continue;
        auto* object = runtime.QueryGameObject(id);
        auto* parent = object ? object->Parent() : nullptr;
        std::size_t depth = 0;
        auto* ancestor = parent;
        while (ancestor && ancestor->GetInstanceID() != _RootID) {
            ancestor = ancestor->Parent();
            ++depth;
        }
        if (!ancestor) {
            RemoveNode(id);
            continue;
        }
        parents.try_emplace(parent->GetInstanceID(), Parent{parent, depth});
    }

    // Insert parents before their children, even when a batch reparents both.
    std::vector<Parent> ordered;
    ordered.reserve(parents.size());
    for (const auto& [id, parent] : parents) ordered.push_back(parent);
    std::stable_sort(ordered.begin(), ordered.end(), [](const Parent& left, const Parent& right) {
        return left.Depth < right.Depth;
    });
    for (const auto& group : ordered) {
        const auto found = _Nodes.find(group.Object->GetInstanceID());
        if (found == _Nodes.end()) continue;
        auto* parent = found->second;
        QStandardItem* previous = nullptr;
        for (std::size_t index = 0; index < group.Object->GetChildCount(); ++index) {
            auto* child = group.Object->GetChild(index);
            const auto id = child->GetInstanceID();
            const auto existing = _Nodes.find(id);
            auto* item = existing == _Nodes.end() ? nullptr : existing->second;
            if (!changed.count(id)) {
                if (item && item->parent() == parent) previous = item;
                continue;
            }
            const int row = previous ? previous->row() + 1 : 0;
            if (item && item->parent() == parent && item->row() == row) {
                previous = item;
                continue;
            }
            const bool newItem = item == nullptr;
            if (newItem) item = MakeItem(child, false);
            else if (auto* oldParent = item->parent()) oldParent->takeRow(item->row());
            if (!item) continue;
            parent->insertRow(previous ? previous->row() + 1 : 0, item);
            if (newItem) BuildChildren(child, item);
            previous = item;
        }
    }
}

void HierarchyTreeModel::UpdateNode(GameSproutRuntime::IGameObject& object, bool includeDescendants) {
    std::vector<GameSproutRuntime::IGameObject*> pending{&object};
    while (!pending.empty()) {
        auto* current = pending.back();
        pending.pop_back();
        const auto id = current->GetInstanceID();
        const auto found = _Nodes.find(id);
        if (found == _Nodes.end()) continue;
        auto* item = found->second;
        if (id != _RootID) {
            const auto name = QString::fromStdString(current->GetName());
            if (name != item->text()) {
                item->setText(name);
                item->setToolTip(QStringLiteral("%1\nID: %2").arg(name).arg(id));
            }
        }
        const bool active = current->IsActiveInHierarchy();
        if (item->data(ActiveRole).toBool() != active) {
            item->setData(active, ActiveRole);
            item->setData(active ? QVariant{} : QVariant::fromValue(QBrush(_InactiveColor)), Qt::ForegroundRole);
        }
        if (includeDescendants)
            for (std::size_t index = 0; index < current->GetChildCount(); ++index)
                pending.push_back(current->GetChild(index));
    }
}

QStringList HierarchyTreeModel::mimeTypes() const {
    return {QString::fromLatin1(SceneObjectDragPayload::MimeType())};
}

QMimeData* HierarchyTreeModel::mimeData(const QModelIndexList& indexes) const {
    QModelIndex selected;
    for (const QModelIndex& index:indexes) {
        if (!index.isValid() || index.column()!=0) continue;
        if (selected.isValid() && selected!=index) return nullptr;
        selected=index;
    }
    if (!selected.isValid() || !_DragSource) return nullptr;
    return _DragSource(
        selected.data(SceneIDRole).toULongLong(),
        selected.data(NodeIDRole).toULongLong());
}

QModelIndex HierarchyTreeModel::FindNode(std::uint64_t id) const {
    const auto found = _Nodes.find(id);
    return found == _Nodes.end() ? QModelIndex{} : found->second->index();
}

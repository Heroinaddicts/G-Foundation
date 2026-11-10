#pragma once
#include "GameSproutRuntimeApi.h"
#include <QColor>
#include <QStandardItemModel>
#include <map>
#include <functional>
#include <set>

class HierarchyTreeModel final : public QStandardItemModel {
public:
    enum Role { NodeIDRole = Qt::UserRole + 1, ActiveRole, SceneRootRole, SceneIDRole };
    explicit HierarchyTreeModel(QObject* parent) : QStandardItemModel(parent) {}
    void Build(GameSproutRuntime::IScene* scene, const QColor& inactiveColor,GameSproutRuntime::IGameObject* contentRoot=nullptr);
    void SyncStructure(GameSproutRuntime::IGameSproutRuntime&, const std::set<std::uint64_t>& changed);
    void UpdateNode(GameSproutRuntime::IGameObject&, bool includeDescendants = false);
    QModelIndex FindNode(std::uint64_t id) const;
    std::size_t GetNodeCount() const { return _Nodes.size(); }
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override { return Qt::CopyAction; }
    using fDragSource = std::function<QMimeData*(std::uint64_t worldID,std::uint64_t objectID)>;
    void SetDragSource(const fDragSource& source) { _DragSource=source; }

private:
    QStandardItem* MakeItem(GameSproutRuntime::IGameObject* object, bool sceneRoot);
    void BuildChildren(GameSproutRuntime::IGameObject*, QStandardItem*);
    void RemoveNode(std::uint64_t id);
    std::map<std::uint64_t, QStandardItem*> _Nodes;
    std::uint64_t _SceneID = 0;
    std::uint64_t _RootID = 0;
    QColor _InactiveColor;
    fDragSource _DragSource;
};

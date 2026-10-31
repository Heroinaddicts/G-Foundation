#pragma once
#include "GameSproutRuntimeApi.h"
#include <QColor>
#include <QStandardItemModel>
#include <map>

class HierarchyTreeModel final : public QStandardItemModel {
public:
    enum Role { NodeIDRole = Qt::UserRole + 1, ActiveRole, SceneRootRole };
    explicit HierarchyTreeModel(QObject* parent) : QStandardItemModel(parent) {}
    void Build(GameSproutRuntime::IScene* scene, const QColor& inactiveColor);
    QModelIndex FindNode(std::uint64_t id) const;
    std::size_t GetNodeCount() const { return _Nodes.size(); }

private:
    QStandardItem* MakeItem(GameSproutRuntime::IGameObject* object, bool sceneRoot, const QColor& inactiveColor);
    std::map<std::uint64_t, QStandardItem*> _Nodes;
};

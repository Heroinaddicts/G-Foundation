#pragma once
#include <QTreeView>
#include <QPersistentModelIndex>
#include <functional>
#include <vector>

// 鼠标按下先只选本地行；真正点击才发布全局选择，拖拽不改变 Inspector 目标。
class ProjectAssetTreeView final : public QTreeView {
    Q_OBJECT
public:
    explicit ProjectAssetTreeView(QWidget* parent=nullptr) : QTreeView(parent) {}
    bool IsPointerSelecting() const { return _PointerSelecting || _Dragging; }
    std::function<void()> ClickCommitted;
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;
private:
    bool _PointerSelecting=false;
    bool _Dragging=false;
    bool _SuppressRelease=false;
    QPersistentModelIndex _PreviousCurrent;
    std::vector<QPersistentModelIndex> _PreviousSelection;
};

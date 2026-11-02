#pragma once
#include <QPersistentModelIndex>
#include <QTreeView>
#include <cstdint>
#include <functional>
#include <vector>

// The local pressed row is a possible drag source. A normal click publishes
// selection on release; an object-reference drag never replaces the Inspector.
class HierarchyTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit HierarchyTreeView(QWidget* parent=nullptr) : QTreeView(parent) {}
    bool IsPointerSelecting() const {return _PointerSelecting || _Dragging || _SuppressRelease;}
    std::function<void()> ClickCommitted;
    std::function<void()> PointerSelectionFinished;
    void CancelPointerSelection(bool restorePrevious=true);
    void reset() override;
    void setModel(QAbstractItemModel* model) override;
    void setSelectionModel(QItemSelectionModel* selection) override;
protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void startDrag(Qt::DropActions supportedActions) override;
    // The native platform drag owns a nested event loop. Isolating that step
    // also lets gesture checks exercise real pointer events without an OS drag.
    virtual void ExecuteDrag(Qt::DropActions supportedActions);
private:
    void RestorePreviousSelection();
    void ClearGesture();
    bool _PointerSelecting=false;
    bool _Dragging=false;
    bool _SuppressRelease=false;
    std::uint64_t _Gesture=0;
    QPersistentModelIndex _PressedIndex;
    QPersistentModelIndex _PreviousCurrent;
    std::vector<QPersistentModelIndex> _PreviousSelection;
};

#include "HierarchyTreeView.h"
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>

void HierarchyTreeView::ClearGesture() {
    _PointerSelecting=false;_Dragging=false;
    _PressedIndex=QPersistentModelIndex{};_PreviousCurrent=QPersistentModelIndex{};
    _PreviousSelection.clear();
}
void HierarchyTreeView::RestorePreviousSelection() {
    if(!selectionModel())return;
    selectionModel()->clearSelection();
    for(const auto& index:_PreviousSelection)
        if(index.isValid()&&index.model()==model())selectionModel()->select(index,QItemSelectionModel::Select|QItemSelectionModel::Rows);
    selectionModel()->setCurrentIndex(_PreviousCurrent.isValid()&&_PreviousCurrent.model()==model()?
        QModelIndex(_PreviousCurrent):QModelIndex{},QItemSelectionModel::NoUpdate);
}
void HierarchyTreeView::CancelPointerSelection(bool restorePrevious) {
    if(!_PointerSelecting&&!_Dragging)return;
    // Keep selection notifications gated while restoring. A model/world change
    // invalidates the gesture and may publish its own authoritative selection.
    if(restorePrevious)RestorePreviousSelection();
    ++_Gesture;ClearGesture();_SuppressRelease=true;
    if(PointerSelectionFinished)PointerSelectionFinished();
}
void HierarchyTreeView::reset() {
    CancelPointerSelection(false);
    QTreeView::reset();
}
void HierarchyTreeView::setModel(QAbstractItemModel* value) {
    CancelPointerSelection(false);
    QTreeView::setModel(value);
}
void HierarchyTreeView::setSelectionModel(QItemSelectionModel* value) {
    CancelPointerSelection(false);
    QTreeView::setSelectionModel(value);
}
void HierarchyTreeView::mousePressEvent(QMouseEvent* event) {
    if(IsPointerSelecting())CancelPointerSelection();
    _SuppressRelease=false;
    if(event->button()==Qt::LeftButton) {
        ++_Gesture;_PointerSelecting=true;
        _PressedIndex=indexAt(event->pos());_PreviousCurrent=currentIndex();_PreviousSelection.clear();
        if(selectionModel())for(const QModelIndex& index:selectionModel()->selectedRows(0))_PreviousSelection.emplace_back(index);
    }
    // Right click retains Qt's immediate selection/context-menu behavior.
    QTreeView::mousePressEvent(event);
}
void HierarchyTreeView::mouseMoveEvent(QMouseEvent* event) {
    // QDrag can return before the source receives the final release. Qt must
    // not turn a trailing held-button move into another pointer selection.
    if(_SuppressRelease&&!_PointerSelecting&&!_Dragging) {event->accept();return;}
    QTreeView::mouseMoveEvent(event);
}
void HierarchyTreeView::mouseReleaseEvent(QMouseEvent* event) {
    if(event->button()!=Qt::LeftButton) {QTreeView::mouseReleaseEvent(event);return;}
    const bool pending=_PointerSelecting;
    const bool suppress=_Dragging||_SuppressRelease;
    const auto gesture=_Gesture;
    const QModelIndex released=indexAt(event->pos());
    // A blank-area click can clear selection, but releasing over another row
    // or outside the viewport is cancellation, not a selection of the press row.
    const bool commit=pending&&!suppress&&viewport()->rect().contains(event->pos())&&
        ((_PressedIndex.isValid()&&released==_PressedIndex)||(!_PressedIndex.isValid()&&!released.isValid()));
    if(!pending&&suppress) {
        // Preserve the authoritative selection at completion time. A world or
        // external selection change may have replaced the original snapshot.
        _PreviousCurrent=currentIndex();_PreviousSelection.clear();
        if(selectionModel())for(const QModelIndex& index:selectionModel()->selectedRows(0))_PreviousSelection.emplace_back(index);
    }
    _PointerSelecting=true;
    QTreeView::mouseReleaseEvent(event);
    if(gesture!=_Gesture||_Dragging)return;
    if((pending&&!commit)||suppress)RestorePreviousSelection();
    if(commit&&!_PressedIndex.isValid()&&selectionModel())selectionModel()->clear();
    ClearGesture();_SuppressRelease=false;
    if(commit&&ClickCommitted)ClickCommitted();
    if(PointerSelectionFinished)PointerSelectionFinished();
}
void HierarchyTreeView::keyPressEvent(QKeyEvent* event) {
    if(IsPointerSelecting()) {
        const bool escape=event->key()==Qt::Key_Escape;
        CancelPointerSelection();
        if(escape) {event->accept();return;}
    }
    _SuppressRelease=false;
    QTreeView::keyPressEvent(event);
}
void HierarchyTreeView::startDrag(Qt::DropActions supportedActions) {
    if(!_PointerSelecting||!_PressedIndex.isValid())return;
    const auto gesture=_Gesture;
    _Dragging=true;_SuppressRelease=true;
    // The pressed row remains selected locally until Qt constructs the MIME
    // payload. Publishing it globally here would destroy the drop field.
    const QPointer<HierarchyTreeView> alive(this);
    ExecuteDrag(supportedActions&Qt::CopyAction);
    if(!alive)return; // The panel may be closed during QDrag's nested loop.
    if(gesture!=_Gesture)return; // A reset/replacement won during the nested loop.
    RestorePreviousSelection();
    ClearGesture();_SuppressRelease=true;
    if(PointerSelectionFinished)PointerSelectionFinished();
}
void HierarchyTreeView::ExecuteDrag(Qt::DropActions supportedActions) {
    QTreeView::startDrag(supportedActions);
}

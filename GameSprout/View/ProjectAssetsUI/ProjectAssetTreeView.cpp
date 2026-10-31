#include "ProjectAssetTreeView.h"
#include <QMouseEvent>
#include <QItemSelectionModel>

void ProjectAssetTreeView::mousePressEvent(QMouseEvent* event) {
    _SuppressRelease=false;
    if(event->button()==Qt::LeftButton) {
        _PointerSelecting=true;
        _PreviousCurrent=currentIndex();_PreviousSelection.clear();
        if(selectionModel())for(const QModelIndex& index:selectionModel()->selectedRows(0))_PreviousSelection.emplace_back(index);
    }
    QTreeView::mousePressEvent(event);
}
void ProjectAssetTreeView::mouseReleaseEvent(QMouseEvent* event) {
    const bool left=event->button()==Qt::LeftButton;
    const bool suppress=_Dragging || _SuppressRelease;
    if(left)_PointerSelecting=suppress;
    QTreeView::mouseReleaseEvent(event);
    if(left && !_Dragging) {
        _PointerSelecting=false;_SuppressRelease=false;
        _PreviousSelection.clear();_PreviousCurrent=QPersistentModelIndex{};
        if(!suppress && ClickCommitted)ClickCommitted();
    }
}
void ProjectAssetTreeView::startDrag(Qt::DropActions supportedActions) {
    _Dragging=true;_SuppressRelease=true;
    // DragOnly/Copy 来自 .ui；引用赋值绝不移动项目文件。
    QTreeView::startDrag(supportedActions & Qt::CopyAction);
    if(selectionModel()) {
        selectionModel()->clearSelection();
        for(const auto& index:_PreviousSelection)if(index.isValid())selectionModel()->select(index,QItemSelectionModel::Select|QItemSelectionModel::Rows);
        selectionModel()->setCurrentIndex(_PreviousCurrent,QItemSelectionModel::NoUpdate);
    }
    _PreviousSelection.clear();_PreviousCurrent=QPersistentModelIndex{};
    _PointerSelecting=false;_Dragging=false;
}

#include "HierarchyUI.h"
#include "ui_HierarchyUI.h"
#include <algorithm>

bool HierarchyUI::AttachHeaderWidget(QWidget* widget) {
    if (!_Window || !_Ui || !widget || widget->parentWidget() ||
        widget == _Window || widget->isAncestorOf(_Window)) return false;
    _HeaderWidgets.push_back(widget);
    QObject::connect(widget,&QObject::destroyed,_Dispatch,[this] {RefreshHeaderVisibility();});
    _Ui->headerLayout->addWidget(widget);
    widget->show();
    RefreshHeaderVisibility();
    return true;
}

bool HierarchyUI::SetHeaderWidgetVisible(QWidget* widget,bool visible) {
    if (!widget || std::none_of(_HeaderWidgets.begin(),_HeaderWidgets.end(),
        [widget](const QPointer<QWidget>& item) {return item == widget;})) return false;
    widget->setVisible(visible);
    RefreshHeaderVisibility();
    return true;
}

bool HierarchyUI::RemoveHeaderWidget(QWidget* widget) {
    const auto found = std::find_if(_HeaderWidgets.begin(),_HeaderWidgets.end(),
        [widget](const QPointer<QWidget>& item) {return widget && item == widget;});
    if (found == _HeaderWidgets.end()) return false;
    _HeaderWidgets.erase(found);
    if (_Window && _Ui) _Ui->headerLayout->removeWidget(widget);
    delete widget;
    RefreshHeaderVisibility();
    return true;
}

void HierarchyUI::RefreshHeaderVisibility() {
    if (!_Window || !_Ui) return;
    _Ui->headerHost->setVisible(std::any_of(_HeaderWidgets.begin(),_HeaderWidgets.end(),
        [](const QPointer<QWidget>& item) {return item && !item->isHidden();}));
}

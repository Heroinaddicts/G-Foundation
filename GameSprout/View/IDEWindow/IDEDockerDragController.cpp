#include "IDEDockerDragController.h"
#include "IDEDockerDropOverlay.h"
#include "IDEDockerManager.h"
#include "IDEDockerFinal.h"
#include "IDEDockerFloatingHost.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTabBar>
#include <QVariant>
#include <algorithm>
#include <iterator>

namespace {
QRect GlobalRect(QWidget* widget) {return QRect(widget->mapToGlobal(QPoint()),widget->size());}
QPoint GlobalMousePosition(QMouseEvent* mouse) {
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    return mouse->globalPosition().toPoint();
#else
    return mouse->globalPos();
#endif
}
bool IsButton(QWidget* widget,QWidget* boundary) {
    for(auto* current=widget;current;current=current->parentWidget()) {
        if(qobject_cast<QAbstractButton*>(current)) return true;
        if(current==boundary) break;
    }
    return false;
}
constexpr DockerDropPosition Edges[]{
    DockerDropPosition::Left, DockerDropPosition::Right,
    DockerDropPosition::Top, DockerDropPosition::Bottom
};

DockerDropPosition LocalEdge(const QRect& content, const QPoint& point) {
    if (!content.contains(point)) return DockerDropPosition::Tabs;
    const float width = qMax(1, qMin(content.width() / 3, 300));
    const float height = qMax(1, qMin(content.height() / 3, 300));
    const float distances[] = {
        (point.x() - content.left()) / width, (content.right() - point.x()) / width,
        (point.y() - content.top()) / height, (content.bottom() - point.y()) / height
    };
    const auto closest = std::min_element(std::begin(distances), std::end(distances));
    return *closest < 1 ? Edges[closest - std::begin(distances)] : DockerDropPosition::Tabs;
}

DockerDropPosition RootEdge(const QRect& workspace, const QPoint& point) {
    constexpr int outer = 28;
    if (!workspace.isValid() || !workspace.adjusted(-outer, -outer, outer, outer).contains(point))
        return DockerDropPosition::Tabs;
    const int distances[] = {
        point.x() - workspace.left(), workspace.right() - point.x(),
        point.y() - workspace.top(), workspace.bottom() - point.y()
    };
    int best = -1;
    for (int edge = 0; edge < 4; ++edge) {
        const int inner = edge < 2 ? 12 : 4;
        if (distances[edge] < -outer || distances[edge] > inner) continue;
        if (best == -1 || qAbs(distances[edge]) < qAbs(distances[best])) best = edge;
    }
    return best == -1 ? DockerDropPosition::Tabs : Edges[best];
}

}

IDEDockerDragController::IDEDockerDragController(IDEDockerManager* manager):QObject(manager),_Manager(manager) {
    setObjectName(QStringLiteral("DockerDragController"));
    _Overlay=new IDEDockerDropOverlay(_Manager->Window());
    QObject::connect(_Overlay,&QObject::destroyed,this,[this] {_Overlay=nullptr;});
    if(qApp) qApp->installEventFilter(this);
    PublishState();
}
IDEDockerDragController::~IDEDockerDragController() {
    CancelDrag();
    if(qApp) qApp->removeEventFilter(this);
    delete _Overlay;
}
void IDEDockerDragController::PublishState() {
    // 集成测试从 QObject 属性读取交互状态，无需依赖 Qt 私有布局或测试专用业务 API。
    setProperty("dragging",_Dragging);setProperty("armed",_Source!=nullptr);
    setProperty("hasDropTarget",_HasTarget);setProperty("rootDrop",_Root);
    setProperty("dropPosition",static_cast<int>(_Position));setProperty("tabInsertionIndex",_TabIndex);
    setProperty("previewGeometry",_Overlay && _Overlay->isVisible()?_Overlay->geometry():QRect());
}
void IDEDockerDragController::CancelDrag(IDEDockerFinal* docker) {
    if(docker && docker!=_Source && docker!=_Target) return;
    auto* grabbed=_GrabWidget.data();
    auto* source=_Source;
    const bool restoreMaximized=!docker && _WasMaximized;
    _Source=nullptr;_Target=nullptr;_GrabWidget=nullptr;_Dragging=false;_HasTarget=false;_Root=false;_TabIndex=-1;
    _Position=DockerDropPosition::Tabs;_PreviewRect={};_TargetHost=nullptr;_TargetBar=nullptr;_WasMaximized=false;
    if(_Overlay) _Overlay->hide();
    // 先清状态再释放，避免 UngrabMouse 事件导致递归取消。
    if(grabbed && QWidget::mouseGrabber()==grabbed) QWidget::mouseGrabber()->releaseMouse();
    if(grabbed && QWidget::keyboardGrabber()==grabbed) QWidget::keyboardGrabber()->releaseKeyboard();
    PublishState();
    if(restoreMaximized && _Manager->Dockers().contains(source) && !_Manager->IsMaximized(source))
        _Manager->ToggleMaximize(source);
}
bool IDEDockerDragController::BeginPress(QWidget* widget,const QPoint& point) {
    auto* window=_Manager->Window();
    if(!widget || IsButton(widget,window)) return false;
    IDEDockerFinal* source=nullptr;
    for(auto* current=widget;current && current!=window;current=current->parentWidget()) {
        auto* bar=qobject_cast<QTabBar*>(current);
        if(!bar || !bar->isVisible()) continue;
        auto* host=qobject_cast<QMainWindow*>(bar->parentWidget());
        if(host!=window && !qobject_cast<IDEDockerFloatingHost*>(host)) continue;
        if(!_Manager->ActiveDocker(bar)) return false;
        const int index=bar->tabAt(bar->mapFromGlobal(point));
        if(index<0) return false;
        bar->setCurrentIndex(index);
        source=_Manager->DockerForTab(bar,index);
        break;
    }
    if(!source) for(auto* docker:_Manager->Dockers()) {
        auto* dock=docker->GetDockWidget();auto* title=dock->titleBarWidget();
        if(!title || !title->isVisible() || !GlobalRect(title).contains(point)) continue;
        if(widget==title || title->isAncestorOf(widget)) {source=docker;break;}
    }
    if(!source || !(source->GetDockWidget()->features()&QDockWidget::DockWidgetMovable)) return false;
    _Source=source;_PressPosition=point;PublishState();return true;
}
QRect IDEDockerDragController::WorkspaceRect(QMainWindow* window) const {
    if(!window || !window->isVisible() || window->isMinimized()) return {};
    QRect result;
    for(auto* docker:_Manager->Dockers()) {
        auto* dock=docker->GetDockWidget();
        if(_Manager->DockHost(dock)!=window || !dock->isVisible() || dock->geometry().left()<0 || dock->geometry().top()<0) continue;
        const QRect rectangle=GlobalRect(dock).intersected(GlobalRect(window));
        result=result.isNull()?rectangle:result.united(rectangle);
    }
    // Dock 区域上方原生 tab 不属于 QDockWidget 的内容几何，将它计入实际工作区。
    for(auto* bar:window->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(bar->isVisible() && _Manager->ActiveDocker(bar)) result=result.isNull()?GlobalRect(bar):result.united(GlobalRect(bar));
    }
    if(result.isValid()) return result;
    result=GlobalRect(window);
    if(auto* menu=window->findChild<QMenuBar*>(QString(),Qt::FindDirectChildrenOnly);menu && menu->isVisible()) result.setTop(result.top()+menu->height());
    if(auto* status=window->findChild<QStatusBar*>(QString(),Qt::FindDirectChildrenOnly);status && status->isVisible()) result.setBottom(result.bottom()-status->height());
    return result;
}
IDEDockerFinal* IDEDockerDragController::OtherTab(IDEDockerFinal* anchor) const {
    if(anchor!=_Source) return anchor;
    auto* host=_Manager->DockHost(_Source->GetDockWidget());
    if(!host) return nullptr;
    const auto peers=host->tabifiedDockWidgets(_Source->GetDockWidget());
    for(auto* candidate:_Manager->Dockers()) if(candidate!=_Source && peers.contains(candidate->GetDockWidget()) &&
        !candidate->GetDockWidget()->isHidden() && !candidate->GetDockWidget()->isFloating()) return candidate;
    return nullptr;
}
void IDEDockerDragController::StartDrag() {
    if(!_Source || _Dragging) return;
    _WasMaximized=_Manager->IsMaximized(_Source);
    _Manager->RestoreMaximized();
    if(!_Source) return;
    _Dragging=true;
    _GrabWidget=_Source->GetDockWidget()->window();
    _GrabWidget->grabMouse(Qt::ClosedHandCursor);_GrabWidget->grabKeyboard();
    PublishState();
}
void IDEDockerDragController::SetTarget(QMainWindow* host, IDEDockerFinal* target,
    DockerDropPosition position, bool root, const QRect& rectangle, int index, QTabBar* bar) {
    if (_HasTarget && _TargetHost == host && _Target == target && _Position == position &&
        _Root == root && _PreviewRect == rectangle && _TabIndex == index && _TargetBar == bar) return;
    _TargetHost=host;_Target=target;_Position=position;_Root=root;_HasTarget=true;
    _TabIndex=index;_TargetBar=bar;_PreviewRect=rectangle;
    if(_Overlay) _Overlay->ShowPreview(rectangle,position,root,bar!=nullptr);
    PublishState();
}

void IDEDockerDragController::UpdateTarget(const QPoint& point) {
    auto* main = _Manager->Window();
    auto* hit = QApplication::widgetAt(point);
    auto* top = hit ? hit->window() : nullptr;
    auto* host = qobject_cast<QMainWindow*>(top);
    IDEDockerFinal* standalone = nullptr;

    if (!host && top) {
        for (auto* docker : _Manager->Dockers())
            if (docker->GetDockWidget() == top && docker->GetDockWidget()->isFloating()) { standalone = docker; break; }
    }
    // Outside an application's client rect widgetAt() may return null.
    // Prefer a floating host's outer activation band over the main one.
    if (!top) {
        for (auto* floating : main->findChildren<IDEDockerFloatingHost*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (floating->isVisible() && !floating->isMinimized() &&
                RootEdge(WorkspaceRect(floating), point) != DockerDropPosition::Tabs) { host = floating; break; }
        }
        if (!host && main->isVisible() && !main->isMinimized()) host = main;
    }
    if (host && host != main && !qobject_cast<IDEDockerFloatingHost*>(host)) host = nullptr;

    if (host) {
        // A native bar exposes a gap between two tabs, including the ends.
        for (auto* bar : host->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!bar->isVisible() || !GlobalRect(bar).contains(point)) continue;
            if (hit && hit != bar && !bar->isAncestorOf(hit)) continue;
            auto* active = _Manager->ActiveDocker(bar);
            auto* target = OtherTab(active);
            if (!target) continue;
            const QPoint local = bar->mapFromGlobal(point);
            const int hovered = bar->tabAt(local);
            const int index = hovered < 0 ? bar->count() : hovered + (local.x() > bar->tabRect(hovered).center().x());
            const int x = qBound(4, index == bar->count() ? bar->tabRect(index - 1).right() + 1 :
                bar->tabRect(index).left(), qMax(4, bar->width() - 4));
            const QRect preview(bar->mapToGlobal(QPoint(x - 4, 0)), QSize(8, bar->height()));
            SetTarget(host, target, DockerDropPosition::Tabs, false, preview, index, bar);
            return;
        }
        const auto edge = RootEdge(WorkspaceRect(host), point);
        if (edge != DockerDropPosition::Tabs) {
            const QRect preview = _Manager->PreviewSplit(_Source, nullptr, edge, true, host);
            if (preview.isValid()) { SetTarget(host, nullptr, edge, true, preview); return; }
        }
    }

    for (auto* docker : _Manager->Dockers()) {
        auto* dock = docker->GetDockWidget();
        if (!dock->isVisible() || dock->isHidden() || !GlobalRect(dock).contains(point)) continue;
        if (standalone ? docker != standalone : !host || _Manager->DockHost(dock) != host ||
            !host->rect().contains(dock->geometry().center())) continue;
        auto* target = OtherTab(docker);
        if (!target) continue;
        QRect content = GlobalRect(dock);
        if (auto* title = dock->titleBarWidget(); title && title->isVisible() && title->height() > 0) {
            const QRect header = GlobalRect(title);
            if (header.contains(point)) {
                SetTarget(host, target, DockerDropPosition::Tabs, false, header);
                return;
            }
            content.setTop(qMax(content.top(), header.bottom() + 1));
        }
        const auto edge = LocalEdge(content, point);
        if (edge != DockerDropPosition::Tabs) {
            const QRect preview = _Manager->PreviewSplit(_Source, target, edge, false, host);
            if (preview.isValid()) { SetTarget(host, target, edge, false, preview); return; }
        }
        break;
    }
    if (_HasTarget) {
        _Target=nullptr;_TargetHost=nullptr;_TargetBar=nullptr;_HasTarget=false;_Root=false;_TabIndex=-1;_PreviewRect={};
        if (_Overlay) _Overlay->hide();
        PublishState();
    }
}

void IDEDockerDragController::FinishDrag(const QPoint& point) {
    if (!_Source) return;
    if (!_Dragging) { CancelDrag(); return; }
    UpdateTarget(point);
    auto* source = _Source;
    auto* target = _Target;
    const auto host = _TargetHost;
    const auto bar = _TargetBar;
    const bool drop = _HasTarget, root = _Root;
    const auto position = _Position;
    const int index = _TabIndex;
    bool inside = _Manager->Window()->isVisible() && !_Manager->Window()->isMinimized() &&
        GlobalRect(_Manager->Window()).contains(point);
    for (auto* docker : _Manager->Dockers()) {
        auto* window = docker->GetDockWidget()->window();
        if (window != _Manager->Window() && window->isVisible() && !window->isMinimized() &&
            GlobalRect(window).contains(point)) inside = true;
    }
    _WasMaximized = _WasMaximized && !drop && inside;
    CancelDrag();
    if (drop) {
        if (position == DockerDropPosition::Tabs && bar && _Manager->ReorderTab(bar, source, index)) return;
        _Manager->DropDocker(source, target, position, root, index, {}, -1.0f, host);
    } else if (!inside) {
        _Manager->FloatDocker(source, point);
    }
}
bool IDEDockerDragController::eventFilter(QObject* watched,QEvent* event) {
    if (event->type()==QEvent::ApplicationDeactivate ||
        (event->type()==QEvent::Close && (watched==_Manager->Window() || watched==_TargetHost || watched==_GrabWidget))) {
        CancelDrag();return false;
    }
    if(_Source && event->type()==QEvent::ShortcutOverride) {event->accept();return true;}
    if(_Source && event->type()==QEvent::KeyPress) {
        if(static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) CancelDrag();
        event->accept();return true;
    }
    if(_Dragging && event->type()==QEvent::UngrabMouse && watched==_GrabWidget) {CancelDrag();return false;}
    if(event->type()==QEvent::MouseButtonPress) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()!=Qt::LeftButton) {if(_Source) {CancelDrag();return true;}return false;}
        CancelDrag();
        return BeginPress(qobject_cast<QWidget*>(watched),GlobalMousePosition(mouse));
    }
    if(_Source && event->type()==QEvent::MouseMove) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if(!(mouse->buttons()&Qt::LeftButton)) {CancelDrag();return true;}
        const QPoint point=GlobalMousePosition(mouse);
        if(!_Dragging && (point-_PressPosition).manhattanLength()>=QApplication::startDragDistance()) StartDrag();
        if(_Dragging) UpdateTarget(point);
        return true;
    }
    if(_Source && event->type()==QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton) {
        FinishDrag(GlobalMousePosition(static_cast<QMouseEvent*>(event)));return true;
    }
    return false;
}

#include "IDEDockerDragController.h"
#include "IDEDockerDropOverlay.h"
#include "IDEDockerManager.h"
#include "IDEDockerFinal.h"
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
#include <limits>
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
QRect SplitPreview(const QRect& rectangle,DockerDropPosition position,bool root) {
    const bool horizontal=position==DockerDropPosition::Left || position==DockerDropPosition::Right;
    const int available=horizontal?rectangle.width():rectangle.height();
    const int extent=qMax(1,available/(root?4:2));
    switch(position) {
    case DockerDropPosition::Left:return QRect(rectangle.left(),rectangle.top(),extent,rectangle.height());
    case DockerDropPosition::Right:return QRect(rectangle.right()-extent+1,rectangle.top(),extent,rectangle.height());
    case DockerDropPosition::Top:return QRect(rectangle.left(),rectangle.top(),rectangle.width(),extent);
    case DockerDropPosition::Bottom:return QRect(rectangle.left(),rectangle.bottom()-extent+1,rectangle.width(),extent);
    case DockerDropPosition::Tabs:return rectangle;
    }
    return rectangle;
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
    auto* grabbed=_GrabWidget;
    auto* source=_Source;
    const bool restoreMaximized=!docker && _WasMaximized;
    _Source=nullptr;_Target=nullptr;_GrabWidget=nullptr;_Dragging=false;_HasTarget=false;_Root=false;_TabIndex=-1;
    _Position=DockerDropPosition::Tabs;_Workspace={};_WasMaximized=false;
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
        if(!bar || bar->parentWidget()!=window || !bar->isVisible()) continue;
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
    _Source=source;_PressPosition=point;_Workspace=WorkspaceRect();PublishState();return true;
}
QRect IDEDockerDragController::WorkspaceRect() const {
    auto* window=_Manager->Window();
    if(!window || !window->isVisible() || window->isMinimized()) return {};
    QRect result;
    for(auto* docker:_Manager->Dockers()) {
        auto* dock=docker->GetDockWidget();
        if(dock->isFloating() || !dock->isVisible() || dock->geometry().left()<0 || dock->geometry().top()<0) continue;
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
    const auto peers=_Manager->Window()->tabifiedDockWidgets(_Source->GetDockWidget());
    for(auto* candidate:_Manager->Dockers()) if(candidate!=_Source && peers.contains(candidate->GetDockWidget()) &&
        !candidate->GetDockWidget()->isHidden() && !candidate->GetDockWidget()->isFloating()) return candidate;
    return nullptr;
}
void IDEDockerDragController::StartDrag() {
    if(!_Source || _Dragging) return;
    _WasMaximized=_Manager->IsMaximized(_Source);
    _Manager->RestoreMaximized();
    if(!_Source) return;
    _Dragging=true;_Workspace=WorkspaceRect();
    _GrabWidget=_Source->GetDockWidget()->isFloating()?static_cast<QWidget*>(_Source->GetDockWidget()):static_cast<QWidget*>(_Manager->Window());
    _GrabWidget->grabMouse(Qt::ClosedHandCursor);_GrabWidget->grabKeyboard();
    PublishState();
}
void IDEDockerDragController::SetTarget(IDEDockerFinal* target,DockerDropPosition position,bool root,const QRect& rectangle,int index) {
    _Target=target;_Position=position;_Root=root;_HasTarget=true;_TabIndex=index;
    const bool reorder=target && position==DockerDropPosition::Tabs && !_Source->GetDockWidget()->isFloating() &&
        _Manager->Window()->tabifiedDockWidgets(_Source->GetDockWidget()).contains(target->GetDockWidget());
    if(_Overlay) _Overlay->ShowPreview(rectangle,position,root,reorder);
    PublishState();
}
void IDEDockerDragController::UpdateTarget(const QPoint& point) {
    _Target=nullptr;_HasTarget=false;_Root=false;_TabIndex=-1;
    if(_Overlay) _Overlay->hide();
    auto* window=_Manager->Window();
    _Workspace=WorkspaceRect();
    // 根边缘独立于单组，优先命中；容许工作区外侧少量余量，避免细边缘难以命中。
    if(_Workspace.isValid() && _Workspace.adjusted(-8,-8,8,8).contains(point)) {
        const int distances[]={qAbs(point.x()-_Workspace.left()),qAbs(point.x()-_Workspace.right()),
            qAbs(point.y()-_Workspace.top()),qAbs(point.y()-_Workspace.bottom())};
        const auto closest=std::min_element(std::begin(distances),std::end(distances));
        if(*closest<=8) {
            const DockerDropPosition positions[]={DockerDropPosition::Left,DockerDropPosition::Right,DockerDropPosition::Top,DockerDropPosition::Bottom};
            const auto position=positions[closest-std::begin(distances)];
            SetTarget(nullptr,position,true,SplitPreview(_Workspace,position,true));return;
        }
    }
    // 标签栏提供插入缝 [0,count]；身份交给 Manager 解析，不比较标题文字。
    for(auto* bar:window->findChildren<QTabBar*>(QString(),Qt::FindDirectChildrenOnly)) {
        if(!bar->isVisible() || !GlobalRect(bar).contains(point)) continue;
        const QPoint local=bar->mapFromGlobal(point);const int hovered=bar->tabAt(local);
        const int insertion=hovered<0?bar->count():hovered+(local.x()>bar->tabRect(hovered).center().x()?1:0);
        auto* anchor=hovered<0?_Manager->ActiveDocker(bar):_Manager->DockerForTab(bar,hovered);
        auto* target=OtherTab(anchor);
        if(!target) continue;
        const bool sameGroup=window->tabifiedDockWidgets(_Source->GetDockWidget()).contains(target->GetDockWidget());
        auto* active=_Manager->ActiveDocker(bar);
        QRect preview=active?GlobalRect(active->GetDockWidget()):GlobalRect(bar);
        if(sameGroup && bar->count()) {
            const int x=insertion>=bar->count()?bar->tabRect(bar->count()-1).right()+1:bar->tabRect(insertion).left();
            preview=QRect(bar->mapToGlobal(QPoint(x-8,0)),QSize(16,bar->height()));
        }
        SetTarget(target,DockerDropPosition::Tabs,false,preview,insertion);return;
    }
    // 局部四边各占内容区三分之一；角落按归一化距离选择最近边，其余中心合并标签。
    for(auto* docker:_Manager->Dockers()) {
        auto* dock=docker->GetDockWidget();
        if(dock->isFloating() || !dock->isVisible() || dock->geometry().left()<0 || dock->geometry().top()<0) continue;
        const QRect rectangle=GlobalRect(dock);
        if(!rectangle.contains(point)) continue;
        auto* target=OtherTab(docker);if(!target) continue;
        QRect content=rectangle;
        if(auto* title=dock->titleBarWidget();title && title->isVisible() && title->height()>0) {
            const QRect header=GlobalRect(title);
            if(header.contains(point)) {SetTarget(target,DockerDropPosition::Tabs,false,rectangle);return;}
            content.setTop(qMax(content.top(),header.bottom()+1));
        }
        const float x=float(point.x()-content.left())/qMax(1,content.width());
        const float y=float(point.y()-content.top())/qMax(1,content.height());
        const float distances[]={x,1-x,y,1-y};const auto closest=std::min_element(std::begin(distances),std::end(distances));
        DockerDropPosition position=DockerDropPosition::Tabs;
        if(*closest<1.0f/3.0f) {
            const DockerDropPosition positions[]={DockerDropPosition::Left,DockerDropPosition::Right,DockerDropPosition::Top,DockerDropPosition::Bottom};
            position=positions[closest-std::begin(distances)];
        }
        SetTarget(target,position,false,SplitPreview(rectangle,position,false));return;
    }
    PublishState();
}
void IDEDockerDragController::FinishDrag(const QPoint& point) {
    if(!_Source) return;
    if(!_Dragging) {CancelDrag();return;}
    UpdateTarget(point);
    auto* source=_Source;auto* target=_Target;
    const bool drop=_HasTarget,root=_Root;const auto position=_Position;const int index=_TabIndex;
    const bool outside=!_Manager->Window()->isVisible() || _Manager->Window()->isMinimized() || !GlobalRect(_Manager->Window()).contains(point);
    // 松开鼠标即结束提交事务，取消清理不再还原拖拽前的最大化布局。
    _WasMaximized=false;
    CancelDrag();
    if(drop) _Manager->DropDocker(source,target,position,root,index);
    else if(outside) _Manager->FloatDocker(source,point);
}
bool IDEDockerDragController::eventFilter(QObject* watched,QEvent* event) {
    if(event->type()==QEvent::ApplicationDeactivate || (watched==_Manager->Window() && event->type()==QEvent::Close)) {
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

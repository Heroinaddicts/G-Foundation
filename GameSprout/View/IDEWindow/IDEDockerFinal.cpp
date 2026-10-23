#include "IDEDockerFinal.h"
#include "IDEDockerManager.h"
#include "IDEDockerTitleBar.h"
#include "IDEMenuFinal.h"
#include "ui_IDEDockerWidget.h"
#include "ui_IDEDockerMenu.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>

IDEDockerFinal::IDEDockerFinal(QMainWindow* window, const QString& name, IDEDockerManager* manager)
    : _Window(window), _Manager(manager) {
    _Dock = new QDockWidget(name, window);
    try {
        _DockUi = new Ui::IDEDockerWidget();
        _DockUi->setupUi(_Dock);
        _Dock->setObjectName(name);
        _Dock->setWindowTitle(name);
        // 关闭时隐藏，保留接口对象以便再次使用。
        _Dock->setAttribute(Qt::WA_DeleteOnClose, false);
        _Menu = new QMenu(_Dock);
        _MenuUi = new Ui::IDEDockerMenu();
        _MenuUi->setupUi(_Menu);
        _Menu->setObjectName(QStringLiteral("DockerContextMenu.") + name);
        _CustomSeparator = _MenuUi->DockerCustomSeparator;
        QAction* maximize = _MenuUi->DockerMaximize;
        QObject::connect(maximize, &QAction::triggered, _Menu, [this]() {
            _Manager->ToggleMaximize(this);
        });
        QAction* floating = _MenuUi->DockerFloat;
        QObject::connect(floating, &QAction::triggered, _Menu, [this]() {
            _Manager->RestoreMaximized();
            _Dock->setFloating(!_Dock->isFloating());
            Activate();
        });
        QAction* close = _MenuUi->DockerClose;
        QObject::connect(close, &QAction::triggered, _Menu, [this]() {
            _Manager->RestoreMaximized();
            _Dock->close();
        });
        QMenu* addTab = _MenuUi->DockerAddTab;
        QObject::connect(addTab, &QMenu::aboutToShow, addTab, [this, addTab]() {
            _Manager->PopulateAddTabMenu(addTab, this);
        });
        const QString maximizeText = maximize->text();
        const QString floatingText = floating->text();
        QObject::connect(_Menu, &QMenu::aboutToShow, _Menu,
                         [this, maximize, floating, addTab, maximizeText, floatingText]() {
            maximize->setText(_Manager->IsMaximized(this)
                ? maximize->property("restoreText").toString() : maximizeText);
            floating->setText(_Dock->isFloating()
                ? floating->property("dockText").toString() : floatingText);
            addTab->setEnabled(!_Dock->isFloating());
        });
        _TitleBar = new IDEDockerTitleBar(_Dock, _Menu);
        _Dock->setTitleBarWidget(_TitleBar);
        window->addDockWidget(Qt::LeftDockWidgetArea, _Dock);
        _Manager->RegisterDocker(this);
    } catch (...) {
        _Manager->UnregisterDocker(this);
        delete _MenuUi;
        delete _DockUi;
        delete _Dock;
        throw;
    }
}

IDEDockerFinal::~IDEDockerFinal() {
    _Manager->UnregisterDocker(this);
    for (auto it = _Menus.rbegin(); it != _Menus.rend(); ++it) {
        delete *it;
    }
    delete _MenuUi;
    delete _DockUi;
    // Qt 同时销毁通过 setWidget 挂载的内容控件。
    delete _Dock;
}

bool IDEDockerFinal::AttachWidget(QWidget* const widget) {
    if (!widget || widget == _Dock || widget->isAncestorOf(_Dock)) {
        return false;
    }
    if (_Dock->widget() && _Dock->widget() != widget) {
        return false;
    }
    // 避免把另一个停靠面板正在管理的内容直接抢过来。
    QDockWidget* currentDock = qobject_cast<QDockWidget*>(widget->parentWidget());
    if (currentDock && currentDock != _Dock) {
        return false;
    }
    if (_Dock->widget() != widget) {
        _Dock->setWidget(widget);
    }
    widget->show();
    Activate();
    return true;
}

void IDEDockerFinal::Layout(eDockerLayout layout) {
    Qt::DockWidgetArea area;
    switch (layout) {
    case eDockerLayout::Left: area = Qt::LeftDockWidgetArea; break;
    case eDockerLayout::Right: area = Qt::RightDockWidgetArea; break;
    case eDockerLayout::Top: area = Qt::TopDockWidgetArea; break;
    case eDockerLayout::Bottom: area = Qt::BottomDockWidgetArea; break;
    default: return;
    }
    _Manager->RestoreMaximized();
    _Dock->setFloating(false);
    _Window->removeDockWidget(_Dock);
    _Window->addDockWidget(area, _Dock);
    _Dock->show();
    _Manager->RequestRefresh();
}

bool IDEDockerFinal::Split(IIDEDocker* docker, Qt::Orientation orientation) {
    auto* other = dynamic_cast<IDEDockerFinal*>(docker);
    if (!other || other == this || other->_Window != _Window || _Dock->isFloating() ||
        (orientation != Qt::Horizontal && orientation != Qt::Vertical) ||
        !_Window->tabifiedDockWidgets(_Dock).isEmpty()) {
        return false;
    }
    _Manager->RestoreMaximized();
    other->_Dock->setFloating(false);
    _Window->removeDockWidget(other->_Dock);
    _Window->splitDockWidget(_Dock, other->_Dock, orientation);
    other->_Dock->show();
    _Manager->RequestRefresh();
    return true;
}

bool IDEDockerFinal::Tabify(IIDEDocker* docker) {
    auto* other = dynamic_cast<IDEDockerFinal*>(docker);
    if (!other || other == this || other->_Window != _Window || _Dock->isFloating()) {
        return false;
    }
    _Manager->RestoreMaximized();
    other->_Dock->setFloating(false);
    _Window->removeDockWidget(other->_Dock);
    _Window->tabifyDockWidget(_Dock, other->_Dock);
    other->_Dock->show();
    _Manager->RequestRefresh();
    return true;
}

void IDEDockerFinal::Activate() {
    _Manager->BeforeActivate(this);
    _Dock->show();
    _Dock->raise();
    _Manager->RequestRefresh();
}

IIDEMenu* IDEDockerFinal::CreateChildMenu(const QString& name) {
    if (name.trimmed().isEmpty()) {
        return nullptr;
    }
    auto* menu = new IDEMenuFinal(_Menu, name, false);
    try {
        _Menus.push_back(menu);
    } catch (...) {
        delete menu;
        throw;
    }
    _Menu->insertAction(_CustomSeparator, menu->GetAction());
    _CustomSeparator->setVisible(true);
    return menu;
}

void IDEDockerFinal::SetTabbedTitle(bool tabbed) {
    _TitleBar->SetTabbed(tabbed);
}

void IDEDockerFinal::SetTitle(const QString& title) {
    if (!title.trimmed().isEmpty()) {
        _Dock->setWindowTitle(title);
    }
}

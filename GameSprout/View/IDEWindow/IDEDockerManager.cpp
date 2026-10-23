#include "IDEDockerManager.h"
#include "IDEDockerFinal.h"
#include "IDEDockerOptionsButton.h"

#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QMenu>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

IDEDockerManager::IDEDockerManager(QMainWindow* window, QMenu* windowMenu)
    : QObject(window), _Window(window), _WindowMenu(windowMenu) {
    _Window->installEventFilter(this);
    QObject::connect(_Window, &QMainWindow::tabifiedDockWidgetActivated, this,
                     [this]() { RequestRefresh(); });
}

IDEDockerManager::~IDEDockerManager() {
    for (const TabBar& entry : _TabBars) {
        delete entry.options;
    }
}

void IDEDockerManager::RegisterDocker(IDEDockerFinal* docker) {
    _Dockers.append(docker);
    QDockWidget* dock = docker->GetDockWidget();
    _WindowMenu->addAction(dock->toggleViewAction());
    QObject::connect(dock->toggleViewAction(), &QAction::triggered, this,
                     [this, docker, dock](bool checked) {
        if (checked) {
            docker->Activate();
        } else {
            RestoreMaximized();
            dock->hide();
        }
    });
    dock->installEventFilter(this);
    QObject::connect(dock, &QDockWidget::topLevelChanged, this, [this, docker](bool floating) {
        if (floating) {
            docker->SetTabbedTitle(false);
        }
        RequestRefresh();
    });
    QObject::connect(dock, &QDockWidget::visibilityChanged, this, [this]() { RequestRefresh(); });
    QObject::connect(dock, &QWidget::windowTitleChanged, this, [this]() { RequestRefresh(); });
    RequestRefresh();
}

void IDEDockerManager::UnregisterDocker(IDEDockerFinal* docker) {
    if (!_Dockers.contains(docker)) {
        return;
    }
    RestoreMaximized();
    QDockWidget* dock = docker->GetDockWidget();
    dock->removeEventFilter(this);
    QObject::disconnect(dock, nullptr, this, nullptr);
    QObject::disconnect(dock->toggleViewAction(), nullptr, this, nullptr);
    if (_WindowMenu) {
        _WindowMenu->removeAction(dock->toggleViewAction());
    }
    _Dockers.removeOne(docker);
    // 标签按钮借用了该面板的菜单；移除后重新绑定当前活动面板。
    RequestRefresh();
}

void IDEDockerManager::RequestRefresh() {
    if (_RefreshPending) {
        return;
    }
    _RefreshPending = true;
    // Qt 完成本轮停靠/标签变化后更新装饰；对象销毁会自动取消回调。
    QTimer::singleShot(0, this, [this]() {
        _RefreshPending = false;
        Refresh();
    });
}

bool IDEDockerManager::eventFilter(QObject* watched, QEvent* event) {
    if (_PositioningTabs && qobject_cast<QTabBar*>(watched)) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::LayoutRequest:
    case QEvent::ChildAdded:
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
        RequestRefresh();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

IDEDockerFinal* IDEDockerManager::ActiveDocker(QTabBar* bar) const {
    if (bar->currentIndex() < 0) {
        return nullptr;
    }
    // 通过公开的几何关系定位标签组，不读取 Qt 私有 tabData 或内部对象名。
    const QRect barRect = bar->geometry();
    for (IDEDockerFinal* docker : _Dockers) {
        QDockWidget* dock = docker->GetDockWidget();
        if (dock->isFloating() || dock->isHidden() || dock->geometry().left() < 0 ||
            dock->windowTitle() != bar->tabText(bar->currentIndex())) {
            continue;
        }
        const QRect rect = dock->geometry();
        if (rect.left() <= barRect.center().x() && rect.right() >= barRect.center().x() &&
            qAbs(rect.top() - (barRect.bottom() + 1)) <= 6) {
            return docker;
        }
    }
    return nullptr;
}

void IDEDockerManager::Refresh() {
    for (IDEDockerFinal* docker : _Dockers) {
        QDockWidget* dock = docker->GetDockWidget();
        bool tabbed = false;
        if (!dock->isFloating() && !dock->isHidden()) {
            for (QDockWidget* peer : _Window->tabifiedDockWidgets(dock)) {
                if (!peer->isHidden() && !peer->isFloating()) {
                    tabbed = true;
                    break;
                }
            }
        }
        docker->SetTabbedTitle(tabbed);
    }

    // 只处理主窗口直属的停靠标签栏，不影响 View 内容中的 QTabBar。
    for (QTabBar* bar : _Window->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        bool known = false;
        for (const TabBar& entry : _TabBars) {
            known |= entry.bar == bar;
        }
        if (known) {
            continue;
        }
        auto* button = CreateTabOptions(bar);
        try {
            _TabBars.append({bar, button});
        } catch (...) {
            delete button;
            throw;
        }
        bar->installEventFilter(this);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(bar, &QTabBar::customContextMenuRequested, this,
                         [this, bar](const QPoint& position) {
            const int index = bar->tabAt(position);
            if (index >= 0) {
                bar->setCurrentIndex(index);
            }
            if (IDEDockerFinal* docker = ActiveDocker(bar)) {
                docker->GetContextMenu()->popup(bar->mapToGlobal(position));
            }
        });
        QObject::connect(bar, &QTabBar::currentChanged, this, [this]() { RequestRefresh(); });
        QObject::connect(bar, &QObject::destroyed, this, [this, bar]() {
            for (qsizetype i = _TabBars.size(); i-- > 0;) {
                if (_TabBars[i].bar == bar) {
                    delete _TabBars[i].options;
                    _TabBars.removeAt(i);
                }
            }
        });
    }
    for (TabBar& entry : _TabBars) {
        IDEDockerFinal* active = ActiveDocker(entry.bar);
        if (entry.bar->isHidden() || entry.bar->count() < 2 || !active) {
            entry.options->hide();
            continue;
        }
        // 菜单按钮属于整个停靠区域，与标签栏并排；给它预留独立空间，
        // 标签从按钮右侧开始排列，过多时仍由原生 QTabBar 滚动显示。
        const QRect dockRect = active->GetDockWidget()->geometry();
        const int width = qMax(entry.options->minimumWidth(), entry.options->sizeHint().width());
        const QRect header(dockRect.left(), entry.bar->y(), dockRect.width(), entry.bar->height());
        _PositioningTabs = true;
        entry.options->setGeometry(header.left(), header.top(), width, header.height());
        entry.bar->setGeometry(header.adjusted(width, 0, 0, 0));
        _PositioningTabs = false;
        entry.options->show();
        entry.options->raise();
    }
}

QToolButton* IDEDockerManager::CreateTabOptions(QTabBar* bar) {
    // 与标签栏同为主窗口的子控件，由管理器清理，不交给单个标签管理。
    auto* button = new IDEDockerOptionsButton(_Window);
    QObject::connect(button, &QToolButton::clicked, this, [this, bar, button]() {
        if (IDEDockerFinal* docker = ActiveDocker(bar)) {
            docker->GetContextMenu()->popup(button->mapToGlobal(QPoint(0, button->height())));
        }
    });
    return button;
}

bool IDEDockerManager::IsMaximized(IDEDockerFinal* docker) const {
    return _Maximized == docker || docker->GetDockWidget()->isMaximized();
}

void IDEDockerManager::RestoreMaximized() {
    if (!_Maximized) {
        return;
    }
    _Maximized = nullptr;
    const QByteArray state = _NormalState;
    _NormalState.clear();
    _Window->restoreState(state, 1);
    RequestRefresh();
}

void IDEDockerManager::BeforeActivate(IDEDockerFinal* docker) {
    if (_Maximized && _Maximized != docker) {
        RestoreMaximized();
    }
}

void IDEDockerManager::ToggleMaximize(IDEDockerFinal* docker) {
    QDockWidget* dock = docker->GetDockWidget();
    if (dock->isFloating()) {
        if (dock->isMaximized()) {
            dock->showNormal();
        } else {
            dock->showMaximized();
        }
        return;
    }
    if (_Maximized == docker) {
        RestoreMaximized();
        return;
    }
    RestoreMaximized();
    _NormalState = _Window->saveState(1);
    _Maximized = docker;
    for (IDEDockerFinal* other : _Dockers) {
        if (other != docker && !other->GetDockWidget()->isFloating()) {
            other->GetDockWidget()->hide();
        }
    }
    dock->show();
    dock->raise();
    RequestRefresh();
}

void IDEDockerManager::PopulateAddTabMenu(QMenu* menu, IDEDockerFinal* anchor) {
    menu->clear();
    for (IDEDockerFinal* candidate : _Dockers) {
        if (candidate == anchor) {
            continue;
        }
        QDockWidget* dock = candidate->GetDockWidget();
        QAction* action = menu->addAction(dock->windowTitle());
        action->setEnabled(dock->isHidden() ||
                           !_Window->tabifiedDockWidgets(anchor->GetDockWidget()).contains(dock));
        QObject::connect(action, &QAction::triggered, menu, [anchor, candidate]() {
            if (anchor->Tabify(candidate)) {
                candidate->Activate();
            }
        });
    }
}

#include "IDEDockerManager.h"
#include "IDEDockerFinal.h"
#include "IDEDockerOptionsButton.h"
#include "IDEDockerDragController.h"

#include <QAction>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QMenu>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QLayout>
#include <QMouseEvent>
#include <QResizeEvent>
#include <utility>

IDEDockerManager::IDEDockerManager(QMainWindow* window, QMenu* windowMenu)
    : QObject(window), _Window(window), _WindowMenu(windowMenu) {
    _Window->installEventFilter(this);
    QObject::connect(_Window, &QMainWindow::tabifiedDockWidgetActivated, this,
                     [this]() { RequestRefresh(); });
    _DragController = new IDEDockerDragController(this);
}

IDEDockerManager::~IDEDockerManager() {
    delete _DragController;
    for (const TabBar& entry : _TabBars) {
        delete entry.options;
    }
}

void IDEDockerManager::RegisterDocker(IDEDockerFinal* docker) {
    _Dockers.append(docker);
    QDockWidget* dock = docker->GetDockWidget();
    _WindowMenu->addAction(dock->toggleViewAction());
    // Qt 的原生 toggle action 先隐藏再发 triggered；菜单高亮时先快照，触发后再确认。
    QObject::connect(dock->toggleViewAction(), &QAction::hovered, this, [this,docker,dock]() {
        if(!dock->isHidden())RememberClosedDocker(docker,false);
    });
    QObject::connect(dock->toggleViewAction(), &QAction::triggered, this,
                     [this, docker, dock](bool checked) {
        if (checked) {
            docker->Activate();
        } else {
            RestoreMaximized();
            RememberClosedDocker(docker);
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
    _DragController->CancelDrag(docker);
    if (_HorizontalDockers.contains(docker)) _HorizontalDockers.clear();
    _HorizontalCenterPeers.removeAll(docker);
    _PreferredDockWidths.remove(docker);
    _ClosedPlacements.remove(docker);
    _TabOrders.remove(docker);
    for(auto it=_TabOrders.begin();it!=_TabOrders.end();++it)it->removeAll(docker);
    for(auto it=_ClosedPlacements.begin();it!=_ClosedPlacements.end();++it) {
        it->Peers.removeAll(docker);it->Span.removeAll(docker);
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

bool IDEDockerManager::SetHorizontalDockStretch(const QList<IDEDockerFinal*>& dockers) {
    if (dockers.size() != 3 || dockers[0] == dockers[1] || dockers[0] == dockers[2] || dockers[1] == dockers[2])
        return false;
    _HorizontalDockers = dockers;
    _HorizontalCenterPeers.clear();
    const auto peers = _Window->tabifiedDockWidgets(dockers[1]->GetDockWidget());
    for (auto* docker : _Dockers)
        if (peers.contains(docker->GetDockWidget())) _HorizontalCenterPeers.append(docker);
    RememberHorizontalDockWidths();
    return true;
}

QList<IDEDockerFinal*> IDEDockerManager::HorizontalDockRow() const {
    if (_HorizontalDockers.size() != 3 || _Maximized) return {};
    const QRect work = _Window->contentsRect();
    const auto isDocked = [this](QDockWidget* dock) {
        return !dock->isFloating() && _Window->dockWidgetArea(dock) != Qt::NoDockWidgetArea;
    };
    const auto onScreen = [&isDocked, &work](QDockWidget* dock) {
        return isDocked(dock) && !dock->isHidden() && work.contains(dock->geometry().center());
    };
    const auto active = [this, &isDocked, &onScreen](IDEDockerFinal* docker) -> IDEDockerFinal* {
        auto* anchor = docker->GetDockWidget();
        if (!isDocked(anchor)) return nullptr;
        if (onScreen(anchor)) return docker;
        const auto peers = _Window->tabifiedDockWidgets(anchor);
        for (auto* candidate : _Dockers)
            if (peers.contains(candidate->GetDockWidget()) && onScreen(candidate->GetDockWidget())) return candidate;
        return nullptr;
    };
    auto* center = active(_HorizontalDockers[1]);
    if (!center) {
        for (auto* peer : _HorizontalCenterPeers) {
            center = active(peer);
            if (center) break;
        }
    }
    if (!center) return {};
    QList<IDEDockerFinal*> row{center};
    const int y = center->GetDockWidget()->geometry().center().y();
    for (auto* docker : _Dockers) {
        auto* dock = docker->GetDockWidget();
        if (docker != center && onScreen(dock) && dock->geometry().contains(dock->geometry().center().x(), y))
            row.append(docker);
    }
    return row;
}

void IDEDockerManager::RememberHorizontalDockWidths() {
    _Window->layout()->activate();
    const auto row = HorizontalDockRow();
    if (row.isEmpty()) return;
    for (auto* docker : row) {
        auto* dock = docker->GetDockWidget();
        _PreferredDockWidths.insert(docker, dock->width());
        const auto peers = _Window->tabifiedDockWidgets(dock);
        for (auto* candidate : _Dockers)
            if (peers.contains(candidate->GetDockWidget())) _PreferredDockWidths.insert(candidate, dock->width());
    }
}

void IDEDockerManager::ResizeHorizontalDocks() {
    _Window->layout()->activate();
    const auto row = HorizontalDockRow();
    if (row.size() < 2) return;
    const auto minimumWidth = [this](QDockWidget* dock) {
        const auto minWidth = [](const QDockWidget* item) {
            return qMax(item->minimumWidth(), item->minimumSizeHint().width());
        };
        int width = minWidth(dock);
        for (auto* peer : _Window->tabifiedDockWidgets(dock))
            if (!peer->isHidden()) width = qMax(width, minWidth(peer));
        return width;
    };
    QList<QDockWidget*> docks;
    QList<int> minimums, widths;
    int available = 0;
    int required = 0;
    int preferred = 0;
    for (auto* docker : row) {
        auto* dock = docker->GetDockWidget();
        const int minimum = minimumWidth(dock);
        const int width = docks.isEmpty() ? minimum : qMax(minimum, _PreferredDockWidths.value(docker, dock->width()));
        docks.append(dock);
        minimums.append(minimum);
        widths.append(width);
        available += dock->width();
        required += minimum;
        preferred += width;
    }
    if (available < required) return;
    if (available < preferred) {
        // Shrink side panes proportionally while retaining their preferred widths for later expansion.
        int extra = available - required;
        int totalFlex = preferred - required;
        for (qsizetype index = 1; index < widths.size() && totalFlex > 0; ++index) {
            const int flex = widths[index] - minimums[index];
            const int share = int(qint64(extra) * flex / totalFlex);
            widths[index] = minimums[index] + share;
            extra -= share;
            totalFlex -= flex;
        }
    } else {
        widths[0] += available - preferred;
    }
    for (qsizetype index = 0; index < docks.size(); ++index) {
        if (docks[index]->width() != widths[index]) {
            _Window->resizeDocks(docks, widths, Qt::Horizontal);
            break;
        }
    }
}

bool IDEDockerManager::eventFilter(QObject* watched, QEvent* event) {
    if(event->type()==QEvent::Close) {
        for(IDEDockerFinal* docker:_Dockers)if(docker->GetDockWidget()==watched){RememberClosedDocker(docker);break;}
    }
    if (watched == _Window && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto cursor = _Window->cursor().shape();
        if (mouse->button() == Qt::LeftButton && (cursor == Qt::SplitVCursor || cursor == Qt::SplitHCursor)) {
            _NativeResize = true;
            _NativeHorizontalResize = cursor == Qt::SplitHCursor;
        }
    } else if (watched == _Window && (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::WindowDeactivate)) {
        if (_NativeResize) {
            _NativeResize = false;
            _RememberHorizontalWidths = std::exchange(_NativeHorizontalResize, false);
            RequestRefresh();
        }
    }
    if (watched == _Window && event->type() == QEvent::Resize) {
        const auto* resize = static_cast<QResizeEvent*>(event);
        if (resize->size().width() != resize->oldSize().width()) _HorizontalResizePending = true;
    }
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
    // Qt 重排后可能缓存仍标记 visible 的旧 tabbar；只有实际位于最上层的标签栏参与交互。
    QWidget* hit=_Window->childAt(bar->geometry().center());
    if(!hit || (hit!=bar&&!bar->isAncestorOf(hit)))return nullptr;
    // 通过公开的几何关系定位标签组，不读取 Qt 私有 tabData 或内部对象名。
    const QRect barRect = bar->geometry();
    for (IDEDockerFinal* docker : _Dockers) {
        QDockWidget* dock = docker->GetDockWidget();
        if (dock->isFloating() || dock->isHidden() || dock->geometry().left() < 0 ||
            dock->geometry().width() <= 0 || dock->windowTitle() != bar->tabText(bar->currentIndex())) {
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

IDEDockerFinal* IDEDockerManager::DockerForTab(QTabBar* bar, int index) const {
    if (!bar || bar->parentWidget() != _Window || index < 0 || index >= bar->count()) return nullptr;
    if (index == bar->currentIndex()) return ActiveDocker(bar);
    // 用公开的标签激活与几何查询取得稳定身份，不读取 Qt 私有 tabData，也不依赖可重名的标题。
    const bool updates = _Window->updatesEnabled();
    const int current = bar->currentIndex();
    _Window->setUpdatesEnabled(false);
    bar->setCurrentIndex(index);
    _Window->layout()->activate();
    IDEDockerFinal* docker = ActiveDocker(bar);
    bar->setCurrentIndex(current);
    _Window->layout()->activate();
    _Window->setUpdatesEnabled(updates);
    return docker;
}

void IDEDockerManager::Refresh() {
    // 原生分隔条拖动时，不与 QMainWindow 的临时拖拽布局争抢标签栏几何。
    if (_NativeResize) return;
    if (std::exchange(_RememberHorizontalWidths, false)) RememberHorizontalDockWidths();
    if (std::exchange(_HorizontalResizePending, false)) ResizeHorizontalDocks();
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
        if(auto* controls=docker->TitleBarWidget()) {
            if(!tabbed) {
                docker->RestoreTitleBarWidget();
                controls->setVisible(!docker->GetDockWidget()->isHidden());
            }else if(!docker->IsContentVisible())controls->hide();
        }
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
        QWidget* controls=active->TitleBarWidget();
        const int controlsWidth=controls?qMax(controls->minimumWidth(),controls->sizeHint().width()):0;
        const QRect header(dockRect.left(), entry.bar->y(), dockRect.width(), entry.bar->height());
        _PositioningTabs = true;
        entry.options->setGeometry(header.left(), header.top(), width, header.height());
        entry.bar->setGeometry(header.adjusted(width, 0, -controlsWidth, 0));
        if(controls) {
            if(controls->parentWidget()!=_Window)controls->setParent(_Window);
            controls->setGeometry(header.right()-controlsWidth+1,header.top(),controlsWidth,header.height());
            controls->show();controls->raise();
        }
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
    _HorizontalResizePending = true;
    RequestRefresh();
}

void IDEDockerManager::BeforeActivate(IDEDockerFinal* docker) {
    if (_Maximized && _Maximized != docker) {
        RestoreMaximized();
    }
    RestoreClosedDocker(docker);
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

#include "IDEWindow.h"
#include "IDEMenuBarFinal.h"
#include "IDEDockerFinal.h"
#include "IDEDockerManager.h"
#include "IDELoadingOverlay.h"
#include "ui_IDEWindow.h"

#include <QApplication>
#include <QDockWidget>
#include <QLayout>
#include <QMainWindow>
#include <QTabWidget>

IDEWindow::~IDEWindow() {
    Release(nullptr);
}

bool IDEWindow::Initialize(GameSproutEditor::GameSproutViewApi * const editor) {
    // 窗口是否显示由 Show/Hide 控制，不在初始化时抢占登录窗口。
    if (_Window) {
        return true;
    }
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
        return false;
    }

    try {
        _Window = new QMainWindow();
        _Ui = new Ui::IDEWindowWindow();
        _Ui->setupUi(_Window);
        _Window->setAttribute(Qt::WA_DeleteOnClose, false);
        // 原生停靠标签位置没有 Designer 属性，通过表单配置适配 Qt 接口。
        _Window->setTabPosition(Qt::AllDockWidgetAreas,
                                _Window->property("dockTabsAtTop").toBool() ? QTabWidget::North : QTabWidget::South);
        _Window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
        _Window->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
        _Window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
        _Window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
        _MenuBar = new IDEMenuBarFinal(_Window->menuBar());
        _DockerManager = new IDEDockerManager(_Window, _Ui->DockerWindowMenu);
        _LoadingOverlay = new IDELoadingOverlay(_Window);
    } catch (...) {
        Release(nullptr);
        throw;
    }
    return _Window != nullptr;
}

bool IDEWindow::Launch(GameSproutEditor::GameSproutViewApi * const editor) {
    return _Window != nullptr;
}

void IDEWindow::LaunchFinished(GameSproutEditor::GameSproutViewApi * const editor) {
    Show();
}

void IDEWindow::Loading(const std::string & title, const std::string & desc) {
    if (!_LoadingOverlay) {
        return;
    }
    if (0 == _LoadingRefCount) {
        _HadImportProgress = !_ImportProgress.empty();
        _LoadingOverlay->Begin(QString::fromStdString(title));
        IncreaseCompletionBudget();
    }
    _LoadingRefCount++;
    _LoadingTitle = title;
    _LoadingDescription = desc;
    RefreshLoadingProgress();
}

void IDEWindow::Unloading() {
    if (_LoadingRefCount <= 0) {
        return;
    }
    _LoadingRefCount--;
    if (0 == _LoadingRefCount) {
        RestoreCompletionBudget();
        _LoadingOverlay->End();
        _ImportProgress.clear();
        _HadImportProgress = false;
    } else RefreshLoadingProgress();
}

void IDEWindow::SetLoadingInfo(const std::string & title, const std::string & desc) {
    _LoadingTitle = title;
    _LoadingDescription = desc;
    RefreshLoadingProgress();
}

void IDEWindow::BindTaskApi(Foundation::ITaskApi* const taskApi) {
    if (_TaskApi == taskApi) return;
    RestoreCompletionBudget();
    _TaskApi = taskApi;
    if (_LoadingRefCount > 0) IncreaseCompletionBudget();
}

void IDEWindow::IncreaseCompletionBudget() {
    if (!_TaskApi || _CompletionBudgetAdjusted) return;
    const UInt64 previous = _TaskApi->GetCompletionBudgetMicroseconds();
    constexpr UInt64 loadingBudget = 12000;
    if (previous >= loadingBudget) return;
    if (_TaskApi->SetCompletionBudgetMicroseconds(loadingBudget)) {
        _SavedCompletionBudgetMicroseconds = previous;
        _CompletionBudgetAdjusted = true;
    }
}

void IDEWindow::RestoreCompletionBudget() {
    if (_TaskApi && _CompletionBudgetAdjusted)
        _TaskApi->SetCompletionBudgetMicroseconds(_SavedCompletionBudgetMicroseconds);
    _CompletionBudgetAdjusted = false;
    _SavedCompletionBudgetMicroseconds = 0;
}

void IDEWindow::Show() {
    if (!_Window) {
        return;
    }
    if (_Window->isMinimized()) {
        _Window->showNormal();
    } else {
        _Window->show();
    }
    _Window->raise();
    _Window->activateWindow();
}

void IDEWindow::Hide() {
    if (_Window) {
        _Window->hide();
    }
}

void IDEWindow::Update(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
    if (_LoadingRefCount > 0) RefreshLoadingProgress();
}

void IDEWindow::Release(GameSproutEditor::GameSproutViewApi * const editor) {
    Q_UNUSED(editor);
    RestoreCompletionBudget();
    _TaskApi = nullptr;
    delete _LoadingOverlay;
    _LoadingOverlay = nullptr;
    _LoadingRefCount = 0;
    _ImportProgress.clear();
    _HadImportProgress = false;
    _LoadingTitle.clear();
    _LoadingDescription.clear();
    // 先释放包装对象，再释放它们依赖的主窗口，避免重复删除 Qt 子控件。
    for (auto it = _Dockers.rbegin(); it != _Dockers.rend(); ++it) {
        delete it->second;
    }
    _Dockers.clear();

    delete _DockerManager;
    _DockerManager = nullptr;

    delete _MenuBar;
    _MenuBar = nullptr;

    delete _Window;
    _Window = nullptr;
    delete _Ui;
    _Ui = nullptr;
}

IIDEMenuBar* IDEWindow::GetMenuBar() const {
    return _MenuBar;
}

IIDEDocker* IDEWindow::CreateDocker(const QString& name) {
    const QString dockName = name.trimmed();
    if (!_Window || dockName.isEmpty()) {
        return nullptr;
    }
    if (IIDEDocker* existing = QueryDocker(dockName)) {
        return existing;
    }

    _DockerManager->RestoreMaximized();
    IDEDockerFinal* docker = new IDEDockerFinal(_Window, dockName, _DockerManager);
    try {
        _Dockers.emplace_back(dockName, docker);
    } catch (...) {
        delete docker;
        throw;
    }
    return docker;
}

IIDEDocker* IDEWindow::QueryDocker(const QString& name) {
    const QString dockName = name.trimmed();
    for (const auto& entry : _Dockers) {
        if (entry.first == dockName) {
            return entry.second;
        }
    }
    return nullptr;
}

bool IDEWindow::ResizeDockers(const QList<IIDEDocker*>& dockers,
                             const QList<int>& sizes, Qt::Orientation orientation) {
    if (!_Window || dockers.isEmpty() || dockers.size() != sizes.size() ||
        (orientation != Qt::Horizontal && orientation != Qt::Vertical)) {
        return false;
    }
    QList<QDockWidget*> nativeDocks;
    qint64 totalWeight = 0;
    for (qsizetype i = 0; i < dockers.size(); ++i) {
        QDockWidget* native = nullptr;
        for (const auto& entry : _Dockers) {
            if (entry.second == dockers[i]) {
                native = static_cast<IDEDockerFinal*>(entry.second)->GetDockWidget();
                break;
            }
        }
        if (!native || native->isFloating() || sizes[i] <= 0 || nativeDocks.contains(native)) {
            return false;
        }
        for (QDockWidget* previous : nativeDocks) {
            if (_Window->tabifiedDockWidgets(native).contains(previous)) return false;
        }
        nativeDocks.append(native);
        totalWeight += sizes[i];
    }
    // resizeDocks 接收像素尺寸。先激活隐藏窗口的布局，再把权重换算为像素，
    // 避免首次 Show 时最小尺寸约束把 19/57/24 这类小数值挤成错误比例。
    _DockerManager->RestoreMaximized();
    _Window->layout()->activate();
    const int extent = orientation == Qt::Horizontal ? _Window->width() : _Window->height();
    QList<int> pixelSizes;
    for (int weight : sizes) {
        pixelSizes.append(qMax(1, int(qint64(weight) * extent / totalWeight)));
    }
    _Window->resizeDocks(nativeDocks, pixelSizes, orientation);
    return true;
}

#include "IDEDockerFloatingHost.h"
#include "ui_IDEDockerFloatingHost.h"

#include <QCloseEvent>
#include <QDockWidget>
#include <QLayout>
#include <QTabWidget>

IDEDockerFloatingHost::IDEDockerFloatingHost(QMainWindow* editor)
    : QMainWindow(editor, Qt::Window), _Editor(editor) {
    Ui::IDEDockerFloatingHostForm ui;
    ui.setupUi(this);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
}

IDEDockerFloatingHost* IDEDockerFloatingHost::For(const QDockWidget* panel) {
    return panel ? qobject_cast<IDEDockerFloatingHost*>(panel->parentWidget()) : nullptr;
}

IDEDockerFloatingHost* IDEDockerFloatingHost::Promote(QMainWindow* editor, QDockWidget* panel) {
    if (auto* host = For(panel)) return host;
    if (!panel || !panel->isFloating()) return nullptr;
    const QRect content(panel->mapToGlobal(QPoint()), panel->size());
    auto* host = new IDEDockerFloatingHost(editor);
    host->setGeometry(content);
    host->setWindowTitle(panel->windowTitle());
    host->AddPanel(panel);
    host->show();
    host->layout()->activate();
    return host;
}

QList<QDockWidget*> IDEDockerFloatingHost::Panels() const {
    return findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly);
}

void IDEDockerFloatingHost::AddPanel(QDockWidget* panel) {
    if (!panel || For(panel) == this) return;
    auto* previous = For(panel);
    if (previous) {
        previous->RemovePanel(panel);
    } else if (auto* mainWindow = qobject_cast<QMainWindow*>(panel->parentWidget())) {
        panel->hide();
        mainWindow->removeDockWidget(panel);
    }
    panel->setParent(this);
    addDockWidget(Qt::LeftDockWidgetArea, panel);
    panel->show();
    if (previous) previous->CloseIfNoVisiblePanels();
}

void IDEDockerFloatingHost::RemovePanel(QDockWidget* panel) {
    if (For(panel) != this) return;
    emit PanelLeaving(panel);
    panel->hide();
    removeDockWidget(panel);
    panel->setParent(_Editor);
}

void IDEDockerFloatingHost::CloseIfNoVisiblePanels() {
    for (auto* panel : Panels()) {
        // A background tab is visible to Qt even while its content is inactive.
        if (!panel->isHidden()) return;
    }
    close();
}

void IDEDockerFloatingHost::closeEvent(QCloseEvent* event) {
    // The Window menu still owns the registered Docker actions. Rehome the
    // hidden panels so opening one later never depends on this window.
    for (auto* panel : Panels()) {
        RemovePanel(panel);
        _Editor->addDockWidget(Qt::LeftDockWidgetArea, panel);
        panel->hide();
    }
    event->accept();
    deleteLater();
}

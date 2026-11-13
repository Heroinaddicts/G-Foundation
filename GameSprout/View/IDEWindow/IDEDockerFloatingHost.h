#pragma once

#include <QMainWindow>

class QCloseEvent;
class QDockWidget;

// A floating workspace owns Qt's native split and tab containers. Docker
// wrappers and their content stay registered with the editor that opened it.
class IDEDockerFloatingHost final : public QMainWindow {
    Q_OBJECT
public:
    static IDEDockerFloatingHost* For(const QDockWidget* panel);
    static IDEDockerFloatingHost* Promote(QMainWindow* editor, QDockWidget* panel);

    void AddPanel(QDockWidget* panel);
    void RemovePanel(QDockWidget* panel);
    void CloseIfNoVisiblePanels();

signals:
    void PanelLeaving(QDockWidget* panel);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    explicit IDEDockerFloatingHost(QMainWindow* editor);
    QList<QDockWidget*> Panels() const;
    QMainWindow* const _Editor;
};

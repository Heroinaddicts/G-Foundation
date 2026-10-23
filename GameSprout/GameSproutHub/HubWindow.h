#ifndef HUBWINDOW_H
#define HUBWINDOW_H

#include "HubProjectManager.h"

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class HubWindow;
}
QT_END_NAMESPACE

class HubWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit HubWindow(QWidget *parent = nullptr);
    ~HubWindow() override;

private:
    void RefreshProjects();
    void CreateProject();
    void ChooseProject();
    void OpenSelectedProject();
    void ShowProjectContextMenu(const QPoint& position);
    void RemoveProjectRecord();
    void OpenProject(const QString& directory, bool isNewProject = false);

    HubWindow(const HubWindow&) = delete;
    HubWindow& operator=(const HubWindow&) = delete;

    HubProjectManager _Projects;
    Ui::HubWindow *ui;
};
#endif // HUBWINDOW_H

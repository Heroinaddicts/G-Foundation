#ifndef GAMESPROUT_IDE_DOCKER_FINAL_H
#define GAMESPROUT_IDE_DOCKER_FINAL_H

#include "IIDEWindow.h"
#include <vector>

QT_BEGIN_NAMESPACE
class QDockWidget;
class QMainWindow;
class QMenu;
class QAction;
QT_END_NAMESPACE
class IDEDockerManager;
class IDEDockerTitleBar;
class IDEMenuFinal;
class IDEContextMenuManagerFinal;
namespace Ui {
class IDEDockerWidget;
class IDEDockerMenu;
}

class IDEDockerFinal final : public IIDEDocker {
public:
    IDEDockerFinal(QMainWindow* window, const QString& name, IDEDockerManager* manager);
    ~IDEDockerFinal() override;

    IDEDockerFinal(const IDEDockerFinal&) = delete;
    IDEDockerFinal& operator=(const IDEDockerFinal&) = delete;

    bool AttachWidget(QWidget* const widget) override;
    void Layout(eDockerLayout layout) override;
    bool Split(IIDEDocker* docker, Qt::Orientation orientation) override;
    bool Tabify(IIDEDocker* docker) override;
    void Activate() override;
    void SetTitle(const QString& title) override;
    IIDEMenu* CreateChildMenu(const QString& name) override;
    IContextMenuManager* GetContextMenuManager() const override;

    QDockWidget* GetDockWidget() const { return _Dock; }
    QMenu* GetContextMenu() const { return _Menu; }
    void SetTabbedTitle(bool tabbed);

private:
    QDockWidget* _Dock = nullptr;
    QMainWindow* _Window = nullptr; // 借用，IDEWindow 先销毁面板再销毁主窗口。
    IDEDockerManager* _Manager = nullptr; // 借用，晚于所有 Docker 销毁。
    IDEDockerTitleBar* _TitleBar = nullptr; // Qt 子控件，随 _Dock 销毁。
    QMenu* _Menu = nullptr;
    QAction* _CustomSeparator = nullptr;
    Ui::IDEDockerWidget* _DockUi = nullptr;
    Ui::IDEDockerMenu* _MenuUi = nullptr;
    std::vector<IDEMenuFinal*> _Menus;
    IDEContextMenuManagerFinal* _ContextMenuManager = nullptr;
};

#endif // GAMESPROUT_IDE_DOCKER_FINAL_H

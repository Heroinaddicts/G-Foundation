#ifndef GAMESPROUT_IDE_DOCKER_MANAGER_H
#define GAMESPROUT_IDE_DOCKER_MANAGER_H

#include <QByteArray>
#include <QList>
#include <QObject>

QT_BEGIN_NAMESPACE
class QMainWindow;
class QMenu;
class QTabBar;
class QToolButton;
QT_END_NAMESPACE
class IDEDockerFinal;

// 只管理通用停靠交互，不负责 Unity 默认布局等业务排布规则。
class IDEDockerManager final : public QObject {
public:
    IDEDockerManager(QMainWindow* window, QMenu* windowMenu);
    ~IDEDockerManager() override;
    void RegisterDocker(IDEDockerFinal* docker);
    void UnregisterDocker(IDEDockerFinal* docker);
    void RequestRefresh();
    void RestoreMaximized();
    void BeforeActivate(IDEDockerFinal* docker);
    void ToggleMaximize(IDEDockerFinal* docker);
    bool IsMaximized(IDEDockerFinal* docker) const;
    void PopulateAddTabMenu(QMenu* menu, IDEDockerFinal* anchor);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct TabBar {
        QTabBar* bar; // 借用 Qt 创建的标签栏。
        QToolButton* options; // 管理器拥有，固定在停靠区域最左侧。
    };
    void Refresh();
    QToolButton* CreateTabOptions(QTabBar* bar);
    IDEDockerFinal* ActiveDocker(QTabBar* bar) const;

    QMainWindow* _Window; // 借用，晚于本对象销毁。
    QMenu* _WindowMenu = nullptr; // 借用 IDEWindow.ui 生成的菜单。
    QList<IDEDockerFinal*> _Dockers; // 全部为借用指针。
    QList<TabBar> _TabBars;
    bool _RefreshPending = false;
    bool _PositioningTabs = false;
    IDEDockerFinal* _Maximized = nullptr;
    QByteArray _NormalState; // 仅供临时最大化还原，不写入文件。
};

#endif

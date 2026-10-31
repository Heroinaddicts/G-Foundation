#ifndef GAMESPROUT_IDE_DOCKER_MANAGER_H
#define GAMESPROUT_IDE_DOCKER_MANAGER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QMap>

QT_BEGIN_NAMESPACE
class QMainWindow;
class QMenu;
class QTabBar;
class QToolButton;
QT_END_NAMESPACE
class IDEDockerFinal;
class IDEDockerDragController;
enum class DockerDropPosition;

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
    QMainWindow* Window() const { return _Window; }
    const QList<IDEDockerFinal*>& Dockers() const { return _Dockers; }
    IDEDockerFinal* ActiveDocker(QTabBar* bar) const;
    IDEDockerFinal* DockerForTab(QTabBar* bar, int index) const;
    bool DropDocker(IDEDockerFinal* source, IDEDockerFinal* target, DockerDropPosition position, bool root, int tabIndex = -1,
                    const QList<IDEDockerFinal*>& targetSpan = {}, float sourceFraction = -1.0f);
    void FloatDocker(IDEDockerFinal* source, const QPoint& globalPosition);
    void RememberClosedDocker(IDEDockerFinal* docker, bool confirmed = true);
    void ForgetClosedDocker(IDEDockerFinal* docker) { _ClosedPlacements.remove(docker); }
    QList<IDEDockerFinal*> TabOrder(IDEDockerFinal* docker) const { return _TabOrders.value(docker); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct TabBar {
        QTabBar* bar; // 借用 Qt 创建的标签栏。
        QToolButton* options; // 管理器拥有，固定在停靠区域最左侧。
    };
    void Refresh();
    QToolButton* CreateTabOptions(QTabBar* bar);
    void RestoreClosedDocker(IDEDockerFinal* docker);
    struct ClosedPlacement {
        QList<IDEDockerFinal*> Peers;
        QList<IDEDockerFinal*> Span;
        int TabIndex = -1;
        DockerDropPosition Position{};
        bool Root = true;
        bool Closed = false;
        float Fraction = 0.5f;
    };
    QMap<IDEDockerFinal*, ClosedPlacement> _ClosedPlacements;
    QMap<IDEDockerFinal*, QList<IDEDockerFinal*>> _TabOrders;

    QMainWindow* _Window; // 借用，晚于本对象销毁。
    QMenu* _WindowMenu = nullptr; // 借用 IDEWindow.ui 生成的菜单。
    QList<IDEDockerFinal*> _Dockers; // 全部为借用指针。
    QList<TabBar> _TabBars;
    bool _RefreshPending = false;
    bool _PositioningTabs = false;
    bool _NativeResize = false;
    IDEDockerDragController* _DragController = nullptr;
    IDEDockerFinal* _Maximized = nullptr;
    QByteArray _NormalState; // 仅供临时最大化还原，不写入文件。
};

#endif

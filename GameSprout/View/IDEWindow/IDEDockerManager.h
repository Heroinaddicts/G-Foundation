#ifndef GAMESPROUT_IDE_DOCKER_MANAGER_H
#define GAMESPROUT_IDE_DOCKER_MANAGER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QMap>
#include <QPointer>
#include <QRect>

QT_BEGIN_NAMESPACE
class QMainWindow;
class QDockWidget;
class QMenu;
class QTabBar;
class QToolButton;
QT_END_NAMESPACE
class IDEDockerFinal;
class IDEDockerDragController;
class IDEDockerFloatingHost;
enum class DockerDropPosition;

// 只管理通用停靠交互，不负责 Unity 默认布局等业务排布规则。
class IDEDockerManager final : public QObject {
public:
    IDEDockerManager(QMainWindow* window, QMenu* windowMenu);
    ~IDEDockerManager() override;
    void RegisterDocker(IDEDockerFinal* docker);
    void UnregisterDocker(IDEDockerFinal* docker);
    void RequestRefresh();
    bool SetHorizontalDockStretch(const QList<IDEDockerFinal*>& dockers);
    void RememberHorizontalDockWidths();
    void RestoreMaximized();
    void BeforeActivate(IDEDockerFinal* docker);
    void ToggleMaximize(IDEDockerFinal* docker);
    bool IsMaximized(IDEDockerFinal* docker) const;
    void PopulateAddTabMenu(QMenu* menu, IDEDockerFinal* anchor);
    QMainWindow* Window() const { return _Window; }
    const QList<IDEDockerFinal*>& Dockers() const { return _Dockers; }
    IDEDockerFinal* ActiveDocker(QTabBar* bar) const;
    IDEDockerFinal* DockerForTab(QTabBar* bar, int index) const;
    bool ReorderTab(QTabBar* bar, IDEDockerFinal* source, int insertion);
    void PlaceTab(QMainWindow* host, IDEDockerFinal* source, int insertion);
    QMainWindow* DockHost(const QDockWidget* dock) const;
    void ObserveFloatingHost(IDEDockerFloatingHost* host);
    QRect PreviewSplit(IDEDockerFinal* source, IDEDockerFinal* target, DockerDropPosition position, bool root,
                       QMainWindow* host = nullptr) const;
    bool DropDocker(IDEDockerFinal* source, IDEDockerFinal* target, DockerDropPosition position, bool root, int tabIndex = -1,
                    const QList<IDEDockerFinal*>& targetSpan = {}, float sourceFraction = -1.0f, QMainWindow* host = nullptr);
    void FloatDocker(IDEDockerFinal* source, const QPoint& globalPosition);
    void RememberClosedDocker(IDEDockerFinal* docker, bool confirmed = true);
    void ForgetClosedDocker(IDEDockerFinal* docker) { _ClosedPlacements.remove(docker); }
    QList<IDEDockerFinal*> TabOrder(IDEDockerFinal* docker) const { return _TabOrders.value(docker); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct TabBar {
        QTabBar* bar; // 借用 Qt 创建的标签栏。
        QPointer<QToolButton> options; // 固定在停靠区域最左侧，可随浮动宿主销毁。
    };
    void Refresh();
    QList<IDEDockerFinal*> HorizontalDockRow() const;
    void ResizeHorizontalDocks();
    QToolButton* CreateTabOptions(QTabBar* bar);
    void RestoreClosedDocker(IDEDockerFinal* docker);
    void RefreshTabBars(QMainWindow* host);
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
    QList<IDEDockerFinal*> _HorizontalDockers;
    QList<IDEDockerFinal*> _HorizontalCenterPeers;
    QMap<IDEDockerFinal*, int> _PreferredDockWidths;
    bool _HorizontalResizePending = false;
    bool _RememberHorizontalWidths = false;
    QList<TabBar> _TabBars;
    bool _RefreshPending = false;
    bool _PositioningTabs = false;
    bool _NativeResize = false;
    bool _NativeHorizontalResize = false;
    IDEDockerDragController* _DragController = nullptr;
    IDEDockerFinal* _Maximized = nullptr;
    QByteArray _NormalState; // 仅供临时最大化还原，不写入文件。
};

#endif

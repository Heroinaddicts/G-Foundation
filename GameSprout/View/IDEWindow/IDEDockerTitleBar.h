#ifndef GAMESPROUT_IDE_DOCKER_TITLE_BAR_H
#define GAMESPROUT_IDE_DOCKER_TITLE_BAR_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QDockWidget;
class QMenu;
QT_END_NAMESPACE
namespace Ui { class IDEDockerTitleBar; }

// 单面板显示标题与菜单按钮；标签组内收起，避免出现第二行标题。
class IDEDockerTitleBar final : public QWidget {
public:
    IDEDockerTitleBar(QDockWidget* dock, QMenu* menu);
    ~IDEDockerTitleBar() override;
    IDEDockerTitleBar(const IDEDockerTitleBar&) = delete;
    IDEDockerTitleBar& operator=(const IDEDockerTitleBar&) = delete;
    void SetTabbed(bool tabbed);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    bool _Tabbed = false;
    Ui::IDEDockerTitleBar* _Ui = nullptr;
    QSize _ExpandedMinimumSize;
    QSize _ExpandedMaximumSize;
};

#endif

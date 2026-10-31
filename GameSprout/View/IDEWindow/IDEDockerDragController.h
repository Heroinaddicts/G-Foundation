#pragma once
#include <QObject>
#include <QPoint>
#include <QRect>

class IDEDockerManager;
class IDEDockerFinal;
class IDEDockerDropOverlay;
class QWidget;
class QTabBar;

enum class DockerDropPosition {Tabs,Left,Right,Top,Bottom};

// 单个 IDE 的停靠拖拽事务。移动期间仅更新预览，释放时由 Manager 提交一次布局操作。
class IDEDockerDragController final : public QObject {
public:
    explicit IDEDockerDragController(IDEDockerManager* manager);
    ~IDEDockerDragController() override;
    IDEDockerDragController(const IDEDockerDragController&)=delete;
    IDEDockerDragController& operator=(const IDEDockerDragController&)=delete;
    void CancelDrag(IDEDockerFinal* docker=nullptr);
    bool IsDragging() const {return _Dragging;}
    IDEDockerFinal* DraggedDocker() const {return _Source;}
    IDEDockerFinal* DropTarget() const {return _Target;}
    bool HasDropTarget() const {return _HasTarget;}
    DockerDropPosition DropPosition() const {return _Position;}
    bool RootDrop() const {return _Root;}
    int TabInsertionIndex() const {return _TabIndex;}

protected:
    bool eventFilter(QObject* watched,QEvent* event) override;

private:
    bool BeginPress(QWidget* watched,const QPoint& globalPosition);
    void StartDrag();
    void UpdateTarget(const QPoint& globalPosition);
    void FinishDrag(const QPoint& globalPosition);
    void PublishState();
    QRect WorkspaceRect() const;
    IDEDockerFinal* OtherTab(IDEDockerFinal* anchor) const;
    void SetTarget(IDEDockerFinal* docker,DockerDropPosition position,bool root,const QRect& rectangle,int tabIndex=-1);
    IDEDockerManager* _Manager; // 借用；Manager 先取消拖拽再销毁 Docker。
    IDEDockerDropOverlay* _Overlay=nullptr;
    IDEDockerFinal* _Source=nullptr;
    IDEDockerFinal* _Target=nullptr;
    QWidget* _GrabWidget=nullptr;
    QPoint _PressPosition;
    QRect _Workspace;
    bool _Dragging=false,_HasTarget=false,_Root=false,_WasMaximized=false;
    DockerDropPosition _Position=DockerDropPosition::Tabs;
    int _TabIndex=-1;
};

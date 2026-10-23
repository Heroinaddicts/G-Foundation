#ifndef __IIDEWindow_h__
#define __IIDEWindow_h__

#include "GameSproutApi.h"

#include <QString>
#include <QList>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

class IIDEMenu {
public:
    virtual ~IIDEMenu() {}

    // 创建叶子菜单项；该项继续创建子项时，转换为子菜单。
    // 返回指针由 IDEWindow 管理，调用方不能 delete。
    virtual IIDEMenu * CreateChildMenu(const QString & name) = 0;

    typedef void(*fMenuClickCallback)();

    // 仅叶子菜单项触发回调；nullptr 清除回调。
    virtual void SetClickCallback(const fMenuClickCallback callback) = 0;
};

class IIDEMenuBar {
public:
    virtual ~IIDEMenuBar() {}

    // 创建顶级菜单，返回指针由 IDEWindow 管理。
    virtual IIDEMenu * CreateChildMenu(const QString & name) = 0;
};

enum class eDockerLayout {
    Left,
    Right,
    Top,
    Bottom,
};

class IIDEDocker {
public:
    virtual ~IIDEDocker() {}

    // 成功后控件交由停靠面板管理，不能再由调用方删除。
    // 拒绝空指针、循环父子关系、其他停靠面板的内容和替换已有内容。
    virtual bool AttachWidget(QWidget * const widget) = 0;

    // 重新停靠到指定区域，同时解除原有标签组/浮动状态并重新显示。
    virtual void Layout(const eDockerLayout layout) = 0;
    // 将 docker 放在本面板右侧/下方，或合并为同一个标签组。
    // 两个面板必须属于同一个 IDEWindow，且不能是同一个面板。
    // Split 的起点必须已停靠且不在标签组中；Tabify 的起点必须已停靠。
    virtual bool Split(IIDEDocker* docker, Qt::Orientation orientation) = 0;
    virtual bool Tabify(IIDEDocker* docker) = 0;
    // 显示并选中本标签，不激活整个应用程序窗口。
    virtual void Activate() = 0;
    // 仅修改显示标题，创建时的 name 仍作为查询和布局标识。
    virtual void SetTitle(const QString& title) = 0;
    // 注册面板专属菜单项，自动出现在该面板的“⋮”菜单中，位于默认操作之前。
    // 支持 CreateChildMenu 创建子菜单，返回指针由 Docker 管理。
    virtual IIDEMenu* CreateChildMenu(const QString& name) = 0;
};

class IIDEWindow : public GameSprout::IGameSproutView {
public:
    virtual ~IIDEWindow() {}

    // Initialize 成功后有效，Release 后返回 nullptr。
    virtual IIDEMenuBar * GetMenuBar() const = 0;
    // 初始停靠左侧；同名返回已有面板；空名称或尚未初始化返回 nullptr。
    // 返回接口由 IDEWindow 管理，所有借用指针在 Release 时失效。
    virtual IIDEDocker * CreateDocker(const QString & name) = 0;

    virtual IIDEDocker * QueryDocker(const QString & name) = 0;
    // sizes 是相对尺寸权重；每个标签组只传一个代表面板。
    // 无效面板、重复面板、非正尺寸和数量不匹配均返回 false。
    virtual bool ResizeDockers(const QList<IIDEDocker*>& dockers,
                               const QList<int>& sizes, Qt::Orientation orientation) = 0;

    // Main thread only, after Initialize and before Release. Each Loading must
    // be paired with one Unloading, including failure/cancellation paths.
    // The first title identifies the batch; later calls update its description.
    // Progress counts completed work items equally; registering more may lower it.
    // Long work must yield to the event loop between steps so the UI can repaint.
    virtual void Loading(const std::string & title, const std::string & desc) = 0;
    virtual void Unloading() = 0;
};

#endif //__IIDEWindow_h__

#ifndef GAMESPROUT_IDE_MENU_FINAL_H
#define GAMESPROUT_IDE_MENU_FINAL_H

#include "IIDEWindow.h"
#include <vector>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
QT_END_NAMESPACE

// 一个菜单节点：没有子项时是命令，有子项时是子菜单。
class IDEMenuFinal final : public IIDEMenu {
public:
    IDEMenuFinal(QWidget* owner, const QString& name, bool submenu);
    ~IDEMenuFinal() override;

    IDEMenuFinal(const IDEMenuFinal&) = delete;
    IDEMenuFinal& operator=(const IDEMenuFinal&) = delete;

    IIDEMenu* CreateChildMenu(const QString& name) override;
    void SetClickCallback(const fMenuClickCallback callback) override;
    QAction* GetAction() const { return _Action; }

private:
    void EnsureSubmenu();

    QWidget* _Owner = nullptr;  // 借用的父控件，销毁本对象前必须保持有效。
    QAction* _Action = nullptr;
    QMenu* _Menu = nullptr;
    fMenuClickCallback _Callback = nullptr;
    std::vector<IDEMenuFinal*> _Children;
};

#endif // GAMESPROUT_IDE_MENU_FINAL_H

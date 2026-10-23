#ifndef GAMESPROUT_IDE_MENU_BAR_FINAL_H
#define GAMESPROUT_IDE_MENU_BAR_FINAL_H

#include "IIDEWindow.h"
#include <vector>

QT_BEGIN_NAMESPACE
class QMenuBar;
QT_END_NAMESPACE

class IDEMenuFinal;

class IDEMenuBarFinal final : public IIDEMenuBar {
public:
    explicit IDEMenuBarFinal(QMenuBar* menuBar);
    ~IDEMenuBarFinal() override;

    IDEMenuBarFinal(const IDEMenuBarFinal&) = delete;
    IDEMenuBarFinal& operator=(const IDEMenuBarFinal&) = delete;

    IIDEMenu* CreateChildMenu(const QString& name) override;

private:
    QMenuBar* _MenuBar = nullptr;  // 借用主窗口的菜单栏。
    std::vector<IDEMenuFinal*> _Menus;
};

#endif // GAMESPROUT_IDE_MENU_BAR_FINAL_H

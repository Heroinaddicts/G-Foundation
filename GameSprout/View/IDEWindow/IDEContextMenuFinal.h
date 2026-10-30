#ifndef GAMESPROUT_IDE_CONTEXT_MENU_FINAL_H
#define GAMESPROUT_IDE_CONTEXT_MENU_FINAL_H

#include "IIDEWindow.h"
#include <vector>

class QAction;
class QMenu;
namespace Ui { class IDEContextMenuItem; }

// 内容右键菜单节点，独立于菜单栏与 Docker 标题栏的 IIDEMenu。
class IDEContextMenuFinal final : public IContextMenu {
public:
    IDEContextMenuFinal(QMenu* parent, const std::string& name);
    ~IDEContextMenuFinal() override;
    IDEContextMenuFinal(const IDEContextMenuFinal&) = delete;
    IDEContextMenuFinal& operator=(const IDEContextMenuFinal&) = delete;

    IContextMenu* CreateChildMenu(const std::string& name) override;
    void SetClickCalback(fMenuClickCallback callback) override;
    const std::string& GetName() const { return _Name; }

private:
    std::string _Name;
    QMenu* _Menu = nullptr;
    QAction* _Action = nullptr; // 由 _Menu 拥有。
    Ui::IDEContextMenuItem* _Ui = nullptr;
    fMenuClickCallback _Callback = nullptr;
    std::vector<IDEContextMenuFinal*> _Children;
};

#endif

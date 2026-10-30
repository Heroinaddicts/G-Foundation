#ifndef GAMESPROUT_IDE_CONTEXT_MENU_MANAGER_FINAL_H
#define GAMESPROUT_IDE_CONTEXT_MENU_MANAGER_FINAL_H

#include "IIDEWindow.h"
#include <QObject>
#include <vector>

class QMenu;
class IDEContextMenuFinal;
namespace Ui { class IDEContextMenu; }

class IDEContextMenuManagerFinal final : public QObject, public IContextMenuManager {
public:
    explicit IDEContextMenuManagerFinal(QWidget* owner);
    ~IDEContextMenuManagerFinal() override;
    IContextMenu* CreateMenu(const std::string& name) override;
    void AttachContent(QWidget* content);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool BelongsToContent(QObject* object) const;
    void WatchChildren(QObject* object);

    QWidget* _Content = nullptr; // Qt 销毁通知清空借用指针。
    QMenu* _Menu = nullptr;
    Ui::IDEContextMenu* _Ui = nullptr;
    std::vector<IDEContextMenuFinal*> _Menus;
};

#endif

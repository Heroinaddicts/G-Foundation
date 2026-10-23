#include "IDEMenuBarFinal.h"
#include "IDEMenuFinal.h"

#include <QMenuBar>

IDEMenuBarFinal::IDEMenuBarFinal(QMenuBar* menuBar) : _MenuBar(menuBar) {
}

IDEMenuBarFinal::~IDEMenuBarFinal() {
    for (auto it = _Menus.rbegin(); it != _Menus.rend(); ++it) {
        delete *it;
    }
    // QMenuBar 自身属于 QMainWindow，不在此处删除。
}

IIDEMenu* IDEMenuBarFinal::CreateChildMenu(const QString& name) {
    if (name.trimmed().isEmpty()) {
        return nullptr;
    }
    IDEMenuFinal* menu = new IDEMenuFinal(_MenuBar, name, true);
    try {
        _Menus.push_back(menu);
    } catch (...) {
        delete menu;
        throw;
    }
    return menu;
}

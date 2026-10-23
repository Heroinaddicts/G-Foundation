#include "IDEMenuFinal.h"

#include <QAction>
#include <QMenu>

IDEMenuFinal::IDEMenuFinal(QWidget* owner, const QString& name, bool submenu)
    : _Owner(owner) {
    try {
        _Action = new QAction(name, owner);
        // 使用 QAction 作为连接上下文，销毁动作时自动断开连接。
        QObject::connect(_Action, &QAction::triggered, _Action, [this]() {
            const fMenuClickCallback callback = _Callback;
            if (!_Menu && callback) {
                callback();
            }
        });
        if (submenu) {
            EnsureSubmenu();
        }
        _Owner->addAction(_Action);
    } catch (...) {
        delete _Action;
        delete _Menu;
        throw;
    }
}

IDEMenuFinal::~IDEMenuFinal() {
    // 子包装对象先销毁，再销毁它们依赖的 Qt 菜单。
    for (auto it = _Children.rbegin(); it != _Children.rend(); ++it) {
        delete *it;
    }
    delete _Action;
    delete _Menu;
}

IIDEMenu* IDEMenuFinal::CreateChildMenu(const QString& name) {
    if (name.trimmed().isEmpty()) {
        return nullptr;
    }

    const bool wasCommand = (_Menu == nullptr);
    IDEMenuFinal* child = nullptr;
    try {
        EnsureSubmenu();
        child = new IDEMenuFinal(_Menu, name, false);
        _Children.push_back(child);
    } catch (...) {
        delete child;
        // 创建失败时恢复原来的叶子节点。
        if (wasCommand) {
            _Action->setMenu(nullptr);
            delete _Menu;
            _Menu = nullptr;
        }
        throw;
    }
    return child;
}

void IDEMenuFinal::SetClickCallback(const fMenuClickCallback callback) {
    _Callback = callback;
}

void IDEMenuFinal::EnsureSubmenu() {
    if (!_Menu) {
        _Menu = new QMenu(_Action->text(), _Owner);
        _Action->setMenu(_Menu);
    }
}

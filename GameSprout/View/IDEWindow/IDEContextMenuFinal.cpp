#include "IDEContextMenuFinal.h"
#include "ui_IDEContextMenuItem.h"
#include <QAction>
#include <QMenu>

IDEContextMenuFinal::IDEContextMenuFinal(QMenu* parent, const std::string& name)
    : _Name(name) {
    try {
        _Menu = new QMenu(parent);
        _Ui = new Ui::IDEContextMenuItem();
        _Ui->setupUi(_Menu);
        _Action = name.empty() ? _Ui->ContextSeparator : _Ui->ContextAction;
        _Action->setText(QString::fromStdString(name));
        _Menu->setTitle(_Action->text());
        QObject::connect(_Action, &QAction::triggered, _Action, [this]() {
            const fMenuClickCallback callback = _Callback;
            if (!_Name.empty() && _Children.empty() && callback) callback();
        });
        parent->addAction(_Action);
    } catch (...) {
        delete _Ui;
        delete _Menu;
        throw;
    }
}

IDEContextMenuFinal::~IDEContextMenuFinal() {
    for (auto it = _Children.rbegin(); it != _Children.rend(); ++it) delete *it;
    delete _Ui;
    delete _Menu;
}

IContextMenu* IDEContextMenuFinal::CreateChildMenu(const std::string& name) {
    if (_Name.empty() || (!name.empty() && QString::fromStdString(name).trimmed().isEmpty())) return nullptr;
    if (!name.empty()) {
        for (IDEContextMenuFinal* child : _Children) {
            if (child->GetName() == name) return child;
        }
    }
    IDEContextMenuFinal* child = new IDEContextMenuFinal(_Menu, name);
    try { _Children.push_back(child); }
    catch (...) { delete child; throw; }
    _Action->setMenu(_Menu);
    _Action->setEnabled(true);
    return child;
}

void IDEContextMenuFinal::SetClickCalback(fMenuClickCallback callback) {
    _Callback = callback;
    if (!_Name.empty() && _Children.empty()) _Action->setEnabled(callback != nullptr);
}

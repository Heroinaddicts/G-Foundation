#include "IDEContextMenuManagerFinal.h"
#include "IDEContextMenuFinal.h"
#include "ui_IDEContextMenu.h"
#include <QAbstractSpinBox>
#include <QChildEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QTextEdit>

IDEContextMenuManagerFinal::IDEContextMenuManagerFinal(QWidget* owner) : QObject(owner) {
    try {
        _Menu = new QMenu(owner);
        _Ui = new Ui::IDEContextMenu();
        _Ui->setupUi(_Menu);
        _Menu->setObjectName(owner->objectName() + QStringLiteral(".ContentContextMenu"));
    } catch (...) {
        delete _Ui;
        delete _Menu;
        throw;
    }
}

IDEContextMenuManagerFinal::~IDEContextMenuManagerFinal() {
    _Menu->hide();
    for (auto it = _Menus.rbegin(); it != _Menus.rend(); ++it) delete *it;
    delete _Ui;
    delete _Menu;
}

IContextMenu* IDEContextMenuManagerFinal::CreateMenu(const std::string& name) {
    if (!name.empty() && QString::fromStdString(name).trimmed().isEmpty()) return nullptr;
    if (!name.empty()) {
        for (IDEContextMenuFinal* menu : _Menus) {
            if (menu->GetName() == name) return menu;
        }
    }
    IDEContextMenuFinal* menu = new IDEContextMenuFinal(_Menu, name);
    try { _Menus.push_back(menu); }
    catch (...) { delete menu; throw; }
    return menu;
}

void IDEContextMenuManagerFinal::AttachContent(QWidget* content) {
    if (_Content == content) return;
    if (_Content) {
        _Content->removeEventFilter(this);
        for (QObject* child : _Content->findChildren<QObject*>()) child->removeEventFilter(this);
        QObject::disconnect(_Content, nullptr, this, nullptr);
    }
    _Menu->hide();
    _Content = content;
    if (!_Content) return;
    WatchChildren(_Content);
    QObject::connect(_Content, &QObject::destroyed, this, [this]() {
        _Content = nullptr;
        _Menu->hide();
    });
}

bool IDEContextMenuManagerFinal::BelongsToContent(QObject* object) const {
    if (!_Content) return false;
    for (QObject* current = object; current; current = current->parent()) {
        if (current == _Content) return true;
    }
    return false;
}

void IDEContextMenuManagerFinal::WatchChildren(QObject* object) {
    object->installEventFilter(this);
    for (QObject* child : object->children()) WatchChildren(child);
}

bool IDEContextMenuManagerFinal::eventFilter(QObject* watched, QEvent* event) {
    if (!BelongsToContent(watched)) return false;
    if (event->type() == QEvent::ChildAdded) {
        // 在构造过程中也可先为 QObject 安装过滤器，不依赖此时的 QWidget 类型。
        WatchChildren(static_cast<QChildEvent*>(event)->child());
        return false;
    }
    if (event->type() != QEvent::ContextMenu || _Menu->isEmpty()) return false;
    QWidget* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return false;
    // 保留 View 主动设置的右键策略，以及输入控件的复制、粘贴等原生菜单。
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (current->contextMenuPolicy() != Qt::DefaultContextMenu ||
            qobject_cast<QLineEdit*>(current) || qobject_cast<QTextEdit*>(current) ||
            qobject_cast<QPlainTextEdit*>(current) || qobject_cast<QAbstractSpinBox*>(current) ||
            qobject_cast<QComboBox*>(current)) return false;
        if (current == _Content) break;
    }
    auto* context = static_cast<QContextMenuEvent*>(event);
    const QPoint position = context->reason() == QContextMenuEvent::Keyboard
        ? widget->mapToGlobal(widget->rect().center()) : context->globalPos();
    _Menu->popup(position);
    context->accept();
    return true;
}

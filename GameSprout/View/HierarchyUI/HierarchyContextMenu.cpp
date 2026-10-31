#include "HierarchyUI.h"
#include "HierarchyTreeModel.h"
#include "ui_HierarchyUI.h"
#include "ui_HierarchyContextMenu.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTreeView>
#include <utility>

namespace {
class HierarchyContextCapture final : public QObject {
public:
    using Callback = std::function<void(const QModelIndex&)>;
    HierarchyContextCapture(QObject* owner, Callback callback,std::function<void()> remove)
        : QObject(owner), _Callback(std::move(callback)), _Remove(std::move(remove)) {}
    void Attach(QTreeView* tree,QWidget* content) {
        _Tree = tree;
        // 在Docker弹出过滤器之前记录本次目标；内容区空白不能沿用上一次右键节点。
        content->removeEventFilter(this);content->installEventFilter(this);
        for(QObject* object:content->findChildren<QObject*>()) {object->removeEventFilter(this);object->installEventFilter(this);}
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ContextMenu) {
            const auto* context = static_cast<QContextMenuEvent*>(event);
            const bool tree=watched==_Tree||watched==_Tree->viewport();
            const QPoint position=_Tree->viewport()->mapFromGlobal(context->globalPos());
            _Callback(context->reason()==QContextMenuEvent::Keyboard ? (tree?_Tree->currentIndex():QModelIndex{})
                : (_Tree->viewport()->rect().contains(position)?_Tree->indexAt(position):QModelIndex{}));
        } else if (event->type() == QEvent::MouseButtonPress && watched == _Tree->viewport()) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::RightButton) _Callback(_Tree->indexAt(mouse->pos()));
        } else if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if((watched==_Tree||watched==_Tree->viewport())&&key->modifiers()==Qt::NoModifier&&
                (key->key()==Qt::Key_Delete||key->key()==Qt::Key_Backspace)) {
                if(!key->isAutoRepeat())_Remove();
                event->accept();return true;
            }
            if (key->key() == Qt::Key_Menu ||
                (key->key() == Qt::Key_F10 && key->modifiers().testFlag(Qt::ShiftModifier)))
                _Callback(_Tree->currentIndex());
        }
        return false;
    }
private:
    QTreeView* _Tree = nullptr;
    Callback _Callback;
    std::function<void()> _Remove;
};
}

void HierarchyUI::InstallContextCapture() {
    if (!_ContextCapture) _ContextCapture = new HierarchyContextCapture(_Dispatch,
        [this](const QModelIndex& index) { RememberContext(index); },[this]{DeleteNode(false);});
    static_cast<HierarchyContextCapture*>(_ContextCapture)->Attach(_Ui->hierarchyTree,_Window);
}

void HierarchyUI::RememberContext(const QModelIndex& index) {
    GameSproutRuntime::IScene* scene = _SceneManager ? _SceneManager->GetActiveScene() : nullptr;
    _ContextSceneID = scene ? scene->GetInstanceID() : 0;
    _ContextObjectID = index.isValid() ? index.data(HierarchyTreeModel::NodeIDRole).toULongLong() : 0;
    _ContextCaptured = true;
    auto* object=_Runtime?_Runtime->QueryGameObject(_ContextObjectID):nullptr;
    if(_DeleteMenu)_DeleteMenu->SetClickCalback(scene&&object&&object!=scene&&object->GetScene()==scene&&
        _SceneManager&&!_SceneManager->HasTransformEdit()?&HierarchyUI::DeleteContextMenu:nullptr);
}

template<HierarchyUI::NodeKind Kind, bool Child>
void HierarchyUI::CreateNodeMenu() { if (g_Self) g_Self->CreateNode(Kind, Child); }

void HierarchyUI::RegisterContextMenus() {
    IContextMenuManager* manager = _Docker->GetContextMenuManager();
    if (!manager) return;
    QMenu definition;
    Ui::HierarchyContextMenu ui;
    ui.setupUi(&definition);
    const std::function<void(const QMenu*, IContextMenu*, bool)> registerItems =
        [&](const QMenu* source, IContextMenu* parent, bool child) {
            for (QAction* action : source->actions()) {
                const std::string name = action->isSeparator() ? "" : action->text().toStdString();
                IContextMenu* menu = parent ? parent->CreateChildMenu(name) : manager->CreateMenu(name);
                if (!menu) continue;
                if (action->menu()) {
                    registerItems(action->menu(), menu, action->menu()->property("createChild").toBool());
                    continue;
                }
                if (action->objectName() == QStringLiteral("actionRefresh")) {
                    menu->SetClickCalback(&HierarchyUI::RefreshMenu);
                    continue;
                }
                if(action->objectName()==QStringLiteral("actionDelete")) {_DeleteMenu=menu;menu->SetClickCalback(nullptr);continue;}
                if (!action->property("nodeKind").isValid()) continue;
                const int kind = action->property("nodeKind").toInt();
                if (kind < 0 || kind >= static_cast<int>(NodeKind::Count)) continue;
                _NodeNames[static_cast<std::size_t>(kind)] = action->property("nodeName").toString().toStdString();
                switch (static_cast<NodeKind>(kind)) {
                case NodeKind::Empty: menu->SetClickCalback(child ? &CreateNodeMenu<NodeKind::Empty,true> : &CreateNodeMenu<NodeKind::Empty,false>); break;
                case NodeKind::Camera: menu->SetClickCalback(child ? &CreateNodeMenu<NodeKind::Camera,true> : &CreateNodeMenu<NodeKind::Camera,false>); break;
                case NodeKind::Mesh: menu->SetClickCalback(child ? &CreateNodeMenu<NodeKind::Mesh,true> : &CreateNodeMenu<NodeKind::Mesh,false>); break;
                case NodeKind::Canvas: menu->SetClickCalback(child ? &CreateNodeMenu<NodeKind::Canvas,true> : &CreateNodeMenu<NodeKind::Canvas,false>); break;
                case NodeKind::Image: menu->SetClickCalback(child ? &CreateNodeMenu<NodeKind::Image,true> : &CreateNodeMenu<NodeKind::Image,false>); break;
                case NodeKind::UIEmpty: menu->SetClickCalback(child ? &CreateNodeMenu<NodeKind::UIEmpty,true> : &CreateNodeMenu<NodeKind::UIEmpty,false>); break;
                case NodeKind::Count: break;
                }
            }
        };
    registerItems(&definition, nullptr, false);
}

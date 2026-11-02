#include "HierarchyTreeModel.h"
#include "HierarchyTreeView.h"
#include "../../View/Shared/SceneObjectDragPayload.h"
#include "GameSproutRuntimeExport.h"
#include <QApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QSortFilterProxyModel>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const char* error) {
    if(!value)throw std::runtime_error(error);
}

class GestureTreeView final : public HierarchyTreeView {
public:
    int DragCount=0;
    Qt::DropActions DragActions;
    SceneObjectDragPayload::Value DragValue;
    bool ValidPayload=false;
    std::function<void()> DuringDrag;
protected:
    void ExecuteDrag(Qt::DropActions actions) override {
        ++DragCount;
        DragActions=actions;
        QMimeData* mime=model()->mimeData(selectionModel()->selectedRows());
        ValidPayload=SceneObjectDragPayload::Decode(mime,DragValue);
        delete mime;
        // Native QDrag::exec also processes queued callbacks. Keep that ordering
        // deterministic in offscreen tests, with the real view's startDrag path.
        QEventLoop nested;
        QTimer::singleShot(0,&nested,[this,&nested] {
            if(DuringDrag)DuringDrag();
            nested.quit();
        });
        nested.exec();
    }
};

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc>1,"Pass Runtime dylib directory");
        runtime=Export(argv[1]);
        Check(runtime,"Cannot load Runtime");
        auto* scene=runtime->CreateScene("Scene");
        auto* mesh=scene->CreateGameObject("Mesh");
        auto* target=scene->CreateGameObject("TestGO");
        const std::uint64_t meshID=mesh->GetInstanceID();
        const std::uint64_t targetID=target->GetInstanceID();
        HierarchyTreeModel model(nullptr);
        model.SetDragSource([&](std::uint64_t worldID,std::uint64_t objectID) {
            return SceneObjectDragPayload::Encode({17,worldID,objectID});
        });
        model.Build(scene,QColor());
        QSortFilterProxyModel proxy;
        proxy.setSourceModel(&model);
        GestureTreeView tree;
        tree.setModel(&proxy);
        tree.setHeaderHidden(true);
        tree.setSelectionMode(QAbstractItemView::SingleSelection);
        tree.setSelectionBehavior(QAbstractItemView::SelectRows);
        tree.setExpandsOnDoubleClick(false);
        tree.setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree.setDragEnabled(true);
        tree.setDragDropMode(QAbstractItemView::DragOnly);
        tree.setDefaultDropAction(Qt::CopyAction);
        tree.resize(480,320);
        tree.expandAll();
        tree.show();
        QTest::qWait(20);

        bool rebuilding=false;
        std::uint64_t inspected=0;
        std::vector<std::uint64_t> published;
        const auto publish=[&](std::uint64_t id) {inspected=id;published.push_back(id);};
        tree.ClickCommitted=[&] {
            if(rebuilding)return;
            const auto selected=tree.selectionModel()->selectedRows();
            publish(selected.empty()?0:selected.front().data(HierarchyTreeModel::NodeIDRole).toULongLong());
        };
        QObject::connect(tree.selectionModel(),&QItemSelectionModel::currentChanged,&tree,
            [&](const QModelIndex& index) {
                if(!rebuilding&&!tree.IsPointerSelecting())
                    publish(index.data(HierarchyTreeModel::NodeIDRole).toULongLong());
            });
        QObject::connect(tree.selectionModel(),&QItemSelectionModel::selectionChanged,&tree,[&] {
            if(rebuilding||tree.IsPointerSelecting())return;
            const auto selected=tree.selectionModel()->selectedRows();
            publish(selected.empty()?0:selected.front().data(HierarchyTreeModel::NodeIDRole).toULongLong());
        });
        const auto select=[&](std::uint64_t id) {
            rebuilding=true;
            tree.selectionModel()->setCurrentIndex(proxy.mapFromSource(model.FindNode(id)),
                QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
            rebuilding=false;
            inspected=id;
            published.clear();
        };
        const auto point=[&](std::uint64_t id) {
            return tree.visualRect(proxy.mapFromSource(model.FindNode(id))).center();
        };
        const auto press=[&](const QPoint& position) {
            QTest::mousePress(tree.viewport(),Qt::LeftButton,Qt::NoModifier,position);
        };
        const auto release=[&](const QPoint& position) {
            QTest::mouseRelease(tree.viewport(),Qt::LeftButton,Qt::NoModifier,position);
        };
        const auto move=[&](const QPoint& position) {
            // QTest provides the real press/release; include the held button on
            // moves so the platform-independent QWidget test exercises Qt drag.
            const QPoint global=tree.viewport()->mapToGlobal(position);
            QMouseEvent event(QEvent::MouseMove,position,global,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(tree.viewport(),&event);
        };
        const auto drag=[&](std::uint64_t id) {
            const QPoint from=point(id);
            press(from);
            move(from+QPoint(2,0));
            move(from+QPoint(QApplication::startDragDistance()+12,0));
            // QAbstractItemView enters DraggingState on the first threshold
            // crossing and starts the drag on a subsequent move.
            move(from+QPoint(QApplication::startDragDistance()+20,0));
        };

        select(meshID);
        press(point(targetID));
        Check(inspected==meshID&&published.empty(),"Mouse-down replaced the Inspector target");
        Check(tree.currentIndex().data(HierarchyTreeModel::NodeIDRole).toULongLong()==targetID,
            "Pressed row was not available to Qt as the drag source");
        release(point(targetID));
        Check(inspected==targetID&&published.size()==1,"Click release did not commit exactly one selection");

        select(meshID);
        press(point(targetID));
        release(point(meshID));
        Check(inspected==meshID&&published.empty(),"Release on another row committed the press row");
        Check(tree.currentIndex().data(HierarchyTreeModel::NodeIDRole).toULongLong()==meshID,
            "Cancelled click did not restore the previous row");
        press(point(targetID));
        release(QPoint(tree.viewport()->width()+8,20));
        Check(inspected==meshID&&published.empty(),"Outside-viewport release committed a selection");

        press(point(targetID));
        QTest::keyClick(&tree,Qt::Key_Escape);
        release(point(targetID));
        Check(inspected==meshID&&published.empty(),"Escape cancellation published the drag source");

        bool nestedKeptInspector=false;
        tree.DuringDrag=[&] {nestedKeptInspector=inspected==meshID&&published.empty()&&tree.IsPointerSelecting();};
        drag(targetID);
        Check(tree.DragCount==1,"Real mouse gesture did not start a drag");
        Check(nestedKeptInspector,"Nested drag callbacks replaced the Inspector target");
        Check(tree.ValidPayload&&tree.DragValue.ObjectID==targetID&&tree.DragValue.WorldID==scene->GetInstanceID(),
            "Drag payload described the inspected Mesh instead of the pressed TestGO");
        Check(tree.DragActions==Qt::CopyAction,"Object reference drag permitted a move");
        Check(inspected==meshID&&published.empty(),"Drag completion published its source");
        release(point(targetID));
        Check(inspected==meshID&&published.empty(),"Post-drag release published its source");
        Check(tree.currentIndex().data(HierarchyTreeModel::NodeIDRole).toULongLong()==meshID,
            "Post-drag release lost the previous local selection");

        // Native cancelled drags return through the same completion path.
        tree.DuringDrag=[&] {QTest::keyClick(&tree,Qt::Key_Escape);};
        drag(targetID);
        release(point(targetID));
        Check(tree.DragCount==2&&inspected==meshID&&published.empty(),"Cancelled drag committed its source");

        tree.DuringDrag=[&] {
            tree.CancelPointerSelection(false);
            select(scene->GetInstanceID());
        };
        drag(targetID);
        release(point(targetID));
        Check(inspected==scene->GetInstanceID()&&published.empty()&&
            tree.currentIndex().data(HierarchyTreeModel::NodeIDRole).toULongLong()==scene->GetInstanceID(),
            "Drag completion restored Mesh over a newer external selection");

        select(meshID);
        QTest::keyClick(&tree,Qt::Key_Down);
        Check(inspected==targetID,"Keyboard selection was delayed like a pointer drag");
        select(meshID);
        QTest::mouseClick(tree.viewport(),Qt::RightButton,Qt::NoModifier,point(targetID));
        Check(inspected==targetID,"Context-menu press did not select its target");

        select(meshID);
        int focused=0;
        QObject::connect(&tree,&QTreeView::doubleClicked,&tree,[&](const QModelIndex& index) {
            if(index.data(HierarchyTreeModel::NodeIDRole).toULongLong()==targetID)++focused;
        });
        QTest::mouseClick(tree.viewport(),Qt::LeftButton,Qt::NoModifier,point(targetID));
        QTest::mouseDClick(tree.viewport(),Qt::LeftButton,Qt::NoModifier,point(targetID));
        release(point(targetID));
        Check(inspected==targetID&&focused==1,"Double-click stopped selecting/focusing the target");

        const QPoint blank(tree.viewport()->width()/2,tree.viewport()->height()-15);
        press(blank);
        Check(inspected==targetID,"Blank mouse-down cleared the Inspector too early");
        release(blank);
        Check(inspected==0,"Blank click did not clear selection");

        select(meshID);
        bool resetDuringDrag=false;
        tree.DuringDrag=[&] {
            rebuilding=true;
            model.Build(nullptr,QColor());
            inspected=0;
            rebuilding=false;
            resetDuringDrag=true;
        };
        drag(targetID);
        release(QPoint(100,50));
        Check(resetDuringDrag&&!tree.IsPointerSelecting(),"Model reset did not cancel the nested drag");
        Check(inspected==0&&published.empty()&&!tree.currentIndex().isValid(),
            "Model reset restored or published an object from the old world");
        tree.setModel(nullptr);
        delete runtime;
        std::cout<<"Hierarchy pointer checks passed: deferred click, real drag gesture, nested callbacks, cancel, outside release, keyboard, context menu, double-click, blank clear and model reset.\n";
        return 0;
    } catch(const std::exception& error) {
        delete runtime;
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

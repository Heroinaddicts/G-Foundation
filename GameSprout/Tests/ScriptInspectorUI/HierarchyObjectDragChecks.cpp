#include "HierarchyTreeModel.h"
#include "../../View/Shared/SceneObjectDragPayload.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <QSortFilterProxyModel>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const char* error) {if(!value)throw std::runtime_error(error);}
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc>1,"Pass Runtime dylib directory");
        runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        auto* scene=runtime->CreateScene("Source world");
        auto* child=scene->CreateGameObject("Dragged node");
        HierarchyTreeModel model(nullptr);
        std::uint64_t sessionID=1;
        model.SetDragSource([&](std::uint64_t worldID,std::uint64_t objectID){
            auto* object=runtime->QueryGameObject(objectID);
            if(!object||!object->GetScene()||object->GetScene()->GetInstanceID()!=worldID)return static_cast<QMimeData*>(nullptr);
            return SceneObjectDragPayload::Encode({sessionID,worldID,objectID});
        });
        model.Build(scene,QColor("#8899aa"));
        QSortFilterProxyModel proxy;proxy.setSourceModel(&model);proxy.setRecursiveFilteringEnabled(true);
        proxy.setFilterFixedString("Dragged");
        const QModelIndex source=model.FindNode(child->GetInstanceID());
        const QModelIndex filtered=proxy.mapFromSource(source);
        Check(filtered.isValid(),"Filtered hierarchy did not preserve child");
        QMimeData* mime=proxy.mimeData({filtered});
        SceneObjectDragPayload::Value value;
        Check(SceneObjectDragPayload::Decode(mime,value),"Hierarchy proxy did not forward its object drag");
        Check(value.SessionID==sessionID&&value.WorldID==scene->GetInstanceID()&&value.ObjectID==child->GetInstanceID(),"Hierarchy drag used a display row or an object address");
        Check(mime->formats()==QStringList{SceneObjectDragPayload::MimeType()},"Hierarchy drag exposes an unrelated payload");
        delete mime;
        mime=model.mimeData({model.FindNode(scene->GetInstanceID())});
        Check(SceneObjectDragPayload::Decode(mime,value)&&value.WorldID==value.ObjectID,"Scene-root drag lost identity");delete mime;
        Check(!model.mimeData({source,model.FindNode(scene->GetInstanceID())}),"Multiple scene objects were silently collapsed");

        int resets=0,inserted=0;
        QObject::connect(&model,&QAbstractItemModel::modelReset,[&]{++resets;});
        QObject::connect(&model,&QAbstractItemModel::rowsInserted,[&](const QModelIndex&,int first,int last){inserted+=last-first+1;});
        const auto rootID=scene->GetInstanceID();
        const QPersistentModelIndex stableRoot(model.FindNode(rootID));
        auto* moved=scene->CreateGameObject("Moved");
        auto* parent=scene->CreateGameObject("Parent");
        auto* sibling=scene->CreateGameObject("Sibling");
        auto* descendant=scene->CreateGameObject("Descendant",moved);
        model.SyncStructure(*runtime,{moved->GetInstanceID(),parent->GetInstanceID(),sibling->GetInstanceID(),descendant->GetInstanceID()});
        Check(inserted==4&&stableRoot.isValid(),"Insertion rebuilt existing hierarchy rows");
        const QPersistentModelIndex stableChild(model.FindNode(child->GetInstanceID()));
        Check(parent->AddChild(moved)&&parent->AddChild(sibling)&&sibling->SetSiblingIndex(0),"Cannot create batched move fixture");
        model.SyncStructure(*runtime,{moved->GetInstanceID(),sibling->GetInstanceID()});
        Check(model.FindNode(moved->GetInstanceID()).parent()==model.FindNode(parent->GetInstanceID())&&
              model.FindNode(moved->GetInstanceID()).row()==1&&model.FindNode(sibling->GetInstanceID()).row()==0&&
              model.FindNode(descendant->GetInstanceID()).parent()==model.FindNode(moved->GetInstanceID()),
              "Batched moves lost sibling order or descendants");
        parent->SetActive(false);
        moved->SetName("Renamed");
        model.UpdateNode(*parent,true);
        Check(!model.FindNode(descendant->GetInstanceID()).data(HierarchyTreeModel::ActiveRole).toBool()&&
              model.FindNode(moved->GetInstanceID()).data().toString()=="Renamed",
              "Incremental refresh missed descendant activation or rename");
        const auto movedID=moved->GetInstanceID(),descendantID=descendant->GetInstanceID();
        Check(scene->DestroyGameObject(moved),"Cannot destroy moved subtree");
        model.SyncStructure(*runtime,{movedID,descendantID});
        Check(!model.FindNode(movedID).isValid()&&!model.FindNode(descendantID).isValid()&&resets==0&&
              stableChild.isValid()&&stableRoot.isValid(),"Incremental edits rebuilt or lost unrelated rows");

        auto* replacement=runtime->CreateScene("Replacement");++sessionID;model.Build(replacement,QColor());
        Check(!model.FindNode(child->GetInstanceID()).isValid(),"Rebuilt hierarchy retained an old drag row");
        mime=model.mimeData({model.FindNode(replacement->GetInstanceID())});
        Check(SceneObjectDragPayload::Decode(mime,value)&&value.SessionID==sessionID&&value.WorldID==replacement->GetInstanceID(),"Rebuilt hierarchy retained its old world identity");delete mime;
        delete runtime;
        std::cout<<"Hierarchy drag checks passed: proxy mapping, object/root identities, single selection and world replacement.\n";
        return 0;
    } catch(const std::exception& error) {delete runtime;std::cerr<<error.what()<<'\n';return 1;}
}

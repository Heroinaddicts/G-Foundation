#include "../NativeScripts/ScriptFieldModuleTestServices.h"
#include "ScriptPropertyRow.h"
#include "ScriptCompositeField.h"
#include "SceneObjectField.h"
#include "../../View/Shared/SceneObjectDragPayload.h"
#include "../../View/Shared/SceneComponentDragPayload.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLineEdit>
#include <QSpinBox>
#include <QToolButton>
#include <QPushButton>
#include <QTreeWidget>
#include <QTest>

class RecursiveUIBehaviour:public GameSproutRuntime::ScriptBehaviour {
public:
    explicit RecursiveUIBehaviour(GameSproutRuntime::IGameSproutRuntime*) {}
    GameSproutRuntime::ScriptValue Tree;
};
static void PumpUI(){QCoreApplication::processEvents();QTest::qWait(5);}
template<typename T>static T* Field(QWidget& parent,const char* id){
    for(auto* field:parent.findChildren<T*>())if(field->property("scriptFieldID").toString()==id)return field;
    return nullptr;
}
int main(int argc,char** argv){
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try{
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime unavailable");
        using GameSproutRuntime::ScriptFieldInfo;using GameSproutRuntime::ScriptValue;using GameSproutRuntime::ScriptValueKind;
        std::string error;
        ScriptFieldInfo body;body.ID="Body";body.Name="Collider";body.ComponentType="GameSproutRuntime.Collider";body.TypeName="IColliderComponent *";body.Default.Kind=ScriptValueKind::Component;
        ScriptFieldInfo name;name.ID="Title";name.Name="Title";name.Default.Kind=ScriptValueKind::String;name.Default.String="First";
        ScriptFieldInfo item;item.ID="Item";item.Name="Item";item.Default.Kind=ScriptValueKind::Structure;item.Children={name,body};item.Default.Fields={{name.ID,name.Default},{body.ID,body.Default}};
        ScriptFieldInfo list;list.ID="Items";list.Name="Items";list.Default.Kind=ScriptValueKind::Sequence;list.Children={item};list.Default.Elements={item.Default};
        ScriptFieldInfo fixed;fixed.ID="Fixed";fixed.Name="Fixed";fixed.Default.Kind=ScriptValueKind::Sequence;fixed.FixedLength=2;
        ScriptFieldInfo number;number.ID="Value";number.Name="Value";number.Default.Kind=ScriptValueKind::Integer;number.Default.Integer=9;fixed.Children={number};fixed.Default.Elements={number.Default,number.Default};
        ScriptFieldInfo key=name;key.ID="Key";key.Name="Key";key.Default.String="a";
        ScriptFieldInfo map;map.ID="Map";map.Name="Map";map.Default.Kind=ScriptValueKind::Dictionary;map.Children={key,number};map.Default.Entries={{key.Default,number.Default}};auto secondKey=key.Default;secondKey.String="b";map.Default.Entries.push_back({secondKey,number.Default});
        ScriptFieldInfo wide;wide.ID="Wide";wide.Name="Wide enum";wide.Default.Kind=ScriptValueKind::Unsigned;wide.Default.Unsigned=UINT64_MAX;wide.EnumValues={{"Max",0,true,UINT64_MAX},{"Zero",0,true,0}};
        ScriptFieldInfo math;math.ID="Tint";math.Name="Color";math.Default.Kind=ScriptValueKind::Color;
        ScriptFieldInfo root;root.ID="Tree";root.Name="Nested settings";root.TypeName="Settings";root.Default.Kind=ScriptValueKind::Structure;root.Children={list,fixed,map,wide,math};for(const auto& field:root.Children)root.Default.Fields[field.ID]=field.Default;
        GameSproutRuntime::ScriptFieldRegistration bridge;bridge.Info=root;
        bridge.Write=[](auto& script,const auto& value,const auto&,auto& error){static_cast<RecursiveUIBehaviour&>(script).Tree=value;error.clear();return true;};
        bridge.Read=[](const auto& script,auto& value,const auto&,auto& error){value=static_cast<const RecursiveUIBehaviour&>(script).Tree;error.clear();return true;};
        bridge.Validate=[](const auto&,const auto&,auto& error){error.clear();return true;};
        GameSproutRuntime::Internal::RegisterScriptClass<RecursiveUIBehaviour>("45678901456789014567890145678901","RecursiveUI","UI.hpp",{"45678901456789014567890145678901",1},{bridge});const auto type=GameSproutRuntime::Internal::ScriptModuleTypes().back();Check(runtime->RegisterScriptType(type,error),error);
        SceneService scenes;scenes.Runtime=runtime;scenes.Scene=runtime->CreateScene("Recursive UI");SelectionService selection;ScriptEditor scripts;ModeService mode;HostService host;ObjectEditing editing;
        editing._Scenes=&scenes;editing._Play=&mode;editing._Host=&host;editing._Runtime=runtime;editing._OwnerThread=QThread::currentThread();editing._Ready=true;
        auto* object=scenes.Scene->CreateGameObject("Owner");auto* target=scenes.Scene->CreateGameObject("Collider object");
        auto* box=runtime->AddBuiltinComponent(target,"GameSproutRuntime.BoxCollider",error);auto* sphere=runtime->AddBuiltinComponent(target,"GameSproutRuntime.SphereCollider",error);Check(box&&sphere,error);
        auto* component=runtime->AddScriptComponent(object,type.Info.TypeID,type.Source,1,error);Check(component,error);
        scripts._Runtime=runtime;scripts._Scenes=&scenes;scripts._Selection=&selection;scripts._Editing=&editing;scripts._Launched=true;scripts._Types.push_back({type.Info,type.Source,"UI.hpp"});
        ScriptPropertySnapshot snapshot;Check(scripts.ReadFields(object->GetInstanceID(),1,snapshot,error),error);
        ComponentEditContext context;context.Scripts=&scripts;context.Scenes=&scenes;context.Editing=&editing;context.Runtime=runtime;context.SceneID=scenes.Scene->GetInstanceID();context.ObjectID=object->GetInstanceID();
        ScriptPropertyRow row(snapshot.Fields[0],snapshot.Target,context,nullptr);row.resize(360,700);row.show();
        std::string lastError;
        const auto refresh=[&]{Check(scripts.ReadFields(object->GetInstanceID(),1,snapshot,error),error);row.Load(snapshot.Fields[0]);};
        row.PathChanged=[&](const auto& path,const auto& value){if(!scripts.SetFieldPath(snapshot.Target,"Tree",path,value,lastError))return;QMetaObject::invokeMethod(&row,refresh,Qt::QueuedConnection);};
        row.ContainerChanged=[&](const auto& path,const auto& edit){if(!scripts.EditContainer(snapshot.Target,"Tree",path,edit,lastError))return;QMetaObject::invokeMethod(&row,refresh,Qt::QueuedConnection);};
        row.Invalid=[&](const auto& message){lastError=message;};PumpUI();
        auto* title=Field<ScriptPropertyRow>(row,"Title");Check(title,"No nested string row");auto* text=title->findChild<QLineEdit*>("scalarEdit");text->setText("Edited");QMetaObject::invokeMethod(text,"editingFinished",Qt::DirectConnection);PumpUI();
        Check(component->GetFields().at("Tree").Fields.at("Items").Elements[0].Fields.at("Title").String=="Edited","Nested .ui string edit did not use Module path transaction");
        Check(scenes.Undo(error),error);refresh();Check(text->text()=="First","Nested Undo did not refresh UI value");
        auto* listUI=Field<ScriptCompositeField>(row,"Items");Check(listUI,"No sequence .ui");
        auto* existingItem=Field<ScriptCompositeField>(row,"Item");Check(existingItem,"No record element foldout");existingItem->findChild<QToolButton*>("foldButton")->setChecked(false);
        QTest::mouseClick(listUI->findChild<QToolButton*>("addButton"),Qt::LeftButton);PumpUI();
        existingItem=Field<ScriptCompositeField>(row,"Item");Check(!existingItem->findChild<QToolButton*>("foldButton")->isChecked(),"Appending a sequence item reset existing foldout state");existingItem->findChild<QToolButton*>("foldButton")->setChecked(true);
        Check(component->GetFields().at("Tree").Fields.at("Items").Elements.size()==2,"Sequence Add did not create a default struct");
        auto* size=listUI->findChild<QSpinBox*>("sizeSpin");size->setValue(3);QMetaObject::invokeMethod(size,"editingFinished",Qt::DirectConnection);PumpUI();Check(component->GetFields().at("Tree").Fields.at("Items").Elements.size()==3,"Sequence Size control did not resize");
        auto* fixedUI=Field<ScriptCompositeField>(row,"Fixed");Check(fixedUI&&!fixedUI->findChild<QSpinBox*>("sizeSpin")->isEnabled()&&!fixedUI->findChild<QToolButton*>("addButton")->isVisible(),"Fixed array exposes length edits");
        auto* mapUI=Field<ScriptCompositeField>(row,"Map");const auto keys=mapUI->findChildren<ScriptPropertyRow*>();int keyIndex=0;
        for(auto* keyRow:keys)if(keyRow->property("scriptFieldID").toString()=="Key"&&keyIndex++==1){auto* edit=keyRow->findChild<QLineEdit*>("scalarEdit");edit->setText("a");QMetaObject::invokeMethod(edit,"editingFinished",Qt::DirectConnection);break;}
        Check(!lastError.empty()&&component->GetFields().at("Tree").Fields.at("Map").Entries[1].first.String=="b","Duplicate map key was accepted by .ui control");refresh();lastError.clear();
        auto* wideUI=Field<ScriptPropertyRow>(row,"Wide");auto* combo=wideUI->findChild<QComboBox*>("enumCombo");Check(combo&&combo->currentText()=="Max","Unsigned enum current value lost precision");
        combo->setCurrentIndex(1);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,1));PumpUI();combo->setCurrentIndex(0);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,0));PumpUI();
        Check(component->GetFields().at("Tree").Fields.at("Wide").Unsigned==UINT64_MAX,"Unsigned enum .ui used a double or signed intermediary");
        auto* reference=Field<ScriptPropertyRow>(row,"Body")->findChild<SceneObjectField*>();Check(reference,"No nested component reference field");
        ScriptObjectTarget dragTarget;Check(scripts.ReadObjectTarget(target->GetInstanceID(),dragTarget,error),error);
        auto* ambiguous=SceneObjectDragPayload::Encode({dragTarget.SessionID,dragTarget.WorldID,dragTarget.ObjectID});
        QDragEnterEvent bad(QPoint(3,3),Qt::CopyAction,ambiguous,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(reference,&bad);Check(!bad.isAccepted()&&scenes.ComponentAllocations==0,"Ambiguous object drag selected/allocated a component");delete ambiguous;
        auto* exact=SceneComponentDragPayload::Encode({{dragTarget.SessionID,dragTarget.WorldID,dragTarget.ObjectID},sphere->GetComponentInstanceID()});
        QDragEnterEvent enter(QPoint(3,3),Qt::CopyAction,exact,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(reference,&enter);Check(enter.isAccepted()&&scenes.ComponentAllocations==0,"Drag hover allocated author FileID");
        QDropEvent drop(QPointF(3,3),Qt::CopyAction,exact,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(reference,&drop);PumpUI();delete exact;
        Check(drop.isAccepted()&&component->GetFields().at("Tree").Fields.at("Items").Elements[0].Fields.at("Body").Component.Assigned&&scenes.ComponentAllocations==1,"Exact component drag did not assign author reference");
        reference=Field<ScriptPropertyRow>(row,"Body")->findChild<SceneObjectField*>();QTest::mouseClick(reference->findChild<QToolButton*>("locateObjectButton"),Qt::LeftButton);Check(selection.SceneSelection.ObjectID==target->GetInstanceID(),"Component locate did not select owner");
        QTest::mouseClick(reference->findChild<QToolButton*>("clearObjectButton"),Qt::LeftButton);PumpUI();Check(!component->GetFields().at("Tree").Fields.at("Items").Elements[0].Fields.at("Body").Component.Assigned,"Component clear failed");
        reference=Field<ScriptPropertyRow>(row,"Body")->findChild<SceneObjectField*>();QTest::mouseClick(reference->findChild<QToolButton*>("pickObjectButton"),Qt::LeftButton);PumpUI();auto* picker=reference->findChild<QDialog*>();Check(picker&&picker->isVisible(),"Component picker did not open");
        auto* tree=picker->findChild<QTreeWidget*>("objectTree");Check(tree->topLevelItemCount()==3,"Component picker omitted exact choices");tree->setCurrentItem(tree->topLevelItem(2));QTest::mouseClick(picker->findChild<QPushButton*>("selectButton"),Qt::LeftButton);PumpUI();Check(component->GetFields().at("Tree").Fields.at("Items").Elements[0].Fields.at("Body").Component.Assigned,"Component picker selection was not committed");
        auto* fold=listUI->findChild<QToolButton*>("foldButton");QTest::mouseClick(fold,Qt::LeftButton);Check(!listUI->findChild<QWidget*>("body")->isVisible(),"Struct/sequence fold did not collapse");QTest::mouseClick(fold,Qt::LeftButton);
        row.resize(360,row.sizeHint().height());row.grab().save("/tmp/gamesprout-recursive-script-fields-360.png");
        row.resize(450,row.sizeHint().height());row.grab().save("/tmp/gamesprout-recursive-script-fields-450.png");
        row.hide();scripts._Launched=false;scripts._Runtime=nullptr;scripts._Scenes=nullptr;scripts._Editing=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS recursive .ui fields, container edits, exact uint64 enum and component picker/drop/clear/locate\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}

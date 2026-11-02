#pragma once
// Integration fixtures deliberately supply a value bridge; production records
// use the compiler's generated native bridges. This exercises Module commands,
// Runtime component identity and author/live transaction isolation together.
class RecursiveFieldsBehaviour final:public GameSproutRuntime::ScriptBehaviour {
public:GameSproutRuntime::ScriptValue Tree;
};
static void CheckRecursiveFieldEditing(ScriptEditor& editor,SceneService& scenes,SelectionService& selection,
    ModeService& mode,GameSproutRuntime::IGameSproutRuntime* runtime,GameSproutRuntime::IGameObject* owner){
    using GameSproutRuntime::ScriptValue;using GameSproutRuntime::ScriptValueKind;using GameSproutRuntime::ScriptFieldInfo;
    std::string error;
    ScriptFieldInfo number;number.ID="Number";number.Name="Number";number.Default.Number=1;
    ScriptFieldInfo reference;reference.ID="Body";reference.Name="Collider";reference.ComponentType="GameSproutRuntime.Collider";reference.TypeName="IColliderComponent *";reference.Default.Kind=ScriptValueKind::Component;
    ScriptFieldInfo part;part.ID="Part";part.Name="Part";part.Default.Kind=ScriptValueKind::Structure;part.Children={number,reference};
    part.Default.Fields={{number.ID,number.Default},{reference.ID,reference.Default}};
    ScriptFieldInfo list;list.ID="List";list.Name="List";list.Default.Kind=ScriptValueKind::Sequence;list.Children={part};list.Default.Elements={part.Default};
    ScriptFieldInfo fixed=list;fixed.ID="Fixed";fixed.Name="Fixed";fixed.FixedLength=2;fixed.Default.Elements={part.Default,part.Default};
    ScriptFieldInfo key;key.ID="Key";key.Name="Key";key.Default.Kind=ScriptValueKind::String;
    ScriptFieldInfo dictionary;dictionary.ID="Map";dictionary.Name="Map";dictionary.Default.Kind=ScriptValueKind::Dictionary;dictionary.Children={key,list};dictionary.Default.Entries={{key.Default,list.Default}};
    ScriptFieldInfo maximum;maximum.ID="Maximum";maximum.Name="Maximum";maximum.TypeName="WideEnum";maximum.Default.Kind=ScriptValueKind::Unsigned;maximum.Default.Unsigned=UINT64_MAX;maximum.EnumValues={{"Maximum",0,true,UINT64_MAX},{"Zero",0,true,0}};
    ScriptFieldInfo root;root.ID="Tree";root.Name="Tree";root.TypeName="NestedRecord";root.Default.Kind=ScriptValueKind::Structure;root.Children={list,fixed,dictionary,maximum};
    for(const auto& field:root.Children)root.Default.Fields[field.ID]=field.Default;
    GameSproutRuntime::ScriptFieldRegistration bridge;bridge.Info=root;
    bridge.Write=[](GameSproutRuntime::IScript& script,const ScriptValue& value,const GameSproutRuntime::ScriptReferenceContext&,std::string& error){static_cast<RecursiveFieldsBehaviour&>(script).Tree=value;error.clear();return true;};
    bridge.Read=[](const GameSproutRuntime::IScript& script,ScriptValue& value,const GameSproutRuntime::ScriptReferenceContext&,std::string& error){value=static_cast<const RecursiveFieldsBehaviour&>(script).Tree;error.clear();return true;};
    bridge.Validate=[](const ScriptValue&,const GameSproutRuntime::ScriptReferenceContext&,std::string& error){error.clear();return true;};
    GameSproutRuntime::Internal::RegisterScriptClass<RecursiveFieldsBehaviour>("01234567012345670123456701234567","RecursiveFields","Nested.hpp",{"01234567012345670123456701234567",1},{bridge});
    const auto type=GameSproutRuntime::Internal::ScriptModuleTypes().back();Check(runtime->RegisterScriptType(type,error),error);
    editor._Types.push_back({type.Info,type.Source,"Nested.hpp"});
    auto* script=runtime->AddScriptComponent(owner,type.Info.TypeID,type.Source,2,error);Check(script,error);
    ScriptPropertySnapshot snapshot;Check(editor.ReadFields(owner->GetInstanceID(),2,snapshot,error),error);const auto scope=snapshot.Target;
    Check(snapshot.Fields.size()==1&&snapshot.Fields[0].Children.size()==4&&snapshot.Fields[0].Children[2].Children.size()==2,"Recursive snapshot omitted fields or dictionary key/value");
    const ScriptFieldPath listPath={{ScriptPathKind::Member,"List"}},fixedPath={{ScriptPathKind::Member,"Fixed"}},mapPath={{ScriptPathKind::Member,"Map"}};
    auto numberPath=listPath;numberPath.push_back({ScriptPathKind::Element,{},0});numberPath.push_back({ScriptPathKind::Member,"Number"});
    auto numberValue=number.Default;numberValue.Number=17;
    Check(editor.SetFieldPath(scope,"Tree",numberPath,numberValue,error),error);
    Check(script->GetFields().at("Tree").Fields.at("List").Elements[0].Fields.at("Number").Number==17,"Nested path did not change the correct leaf");
    Check(scenes.Undo(error)&&script->GetFields().at("Tree").Fields.at("List").Elements[0].Fields.at("Number").Number==1&&scenes.Redo(error),"Recursive field Undo/Redo failed");
    Check(editor.EditContainer(scope,"Tree",listPath,{ScriptContainerAction::Insert,1},error),error);
    Check(editor.EditContainer(scope,"Tree",listPath,{ScriptContainerAction::Move,0,1},error),error);
    Check(script->GetFields().at("Tree").Fields.at("List").Elements[1].Fields.at("Number").Number==17,"Sequence reorder lost the moved value");
    Check(editor.EditContainer(scope,"Tree",listPath,{ScriptContainerAction::Remove,0},error)&&editor.EditContainer(scope,"Tree",listPath,{ScriptContainerAction::Resize,3},error),error);
    Check(script->GetFields().at("Tree").Fields.at("List").Elements.size()==3,"Sequence resize failed");
    Check(!editor.EditContainer(scope,"Tree",listPath,{ScriptContainerAction::Resize,10000},error),"Nested resize exceeded the Runtime total-element budget");
    const auto fixedBefore=script->GetFields().at("Tree").Fields.at("Fixed");
    Check(!editor.EditContainer(scope,"Tree",fixedPath,{ScriptContainerAction::Resize,3},error)&&!editor.EditContainer(scope,"Tree",fixedPath,{ScriptContainerAction::Remove,0},error)&&
        script->GetFields().at("Tree").Fields.at("Fixed")==fixedBefore,"Fixed array changed length");
    Check(editor.EditContainer(scope,"Tree",fixedPath,{ScriptContainerAction::Move,0,1},error),error);
    Check(editor.EditContainer(scope,"Tree",mapPath,{ScriptContainerAction::Insert,1},error),error);
    const auto mapBefore=script->GetFields().at("Tree").Fields.at("Map");
    auto duplicate=mapBefore.Entries[0].first;auto keyPath=mapPath;keyPath.push_back({ScriptPathKind::EntryKey,{},1});
    Check(!editor.SetFieldPath(scope,"Tree",keyPath,duplicate,error)&&script->GetFields().at("Tree").Fields.at("Map")==mapBefore,"Duplicate dictionary key overwrote data");
    auto nestedMapPath=mapPath;nestedMapPath.push_back({ScriptPathKind::EntryValue,{},1});
    Check(editor.EditContainer(scope,"Tree",nestedMapPath,{ScriptContainerAction::Insert,1},error),error);
    auto stale=listPath;stale.push_back({ScriptPathKind::Element,{},999});Check(!editor.SetFieldPath(scope,"Tree",stale,part.Default,error),"Stale array path accepted");
    Check(script->GetFields().at("Tree").Fields.at("Maximum").Unsigned==UINT64_MAX,"Unsigned enum lost precision");
    auto* target=scenes.Scene->CreateGameObject("Two colliders");
    auto* first=runtime->AddBuiltinComponent(target,"GameSproutRuntime.BoxCollider",error);auto* second=runtime->AddBuiltinComponent(target,"GameSproutRuntime.SphereCollider",error);Check(first&&second,error);
    ScriptObjectTarget source;Check(editor.ReadObjectTarget(target->GetInstanceID(),source,error),error);
    std::vector<ScriptComponentChoice> choices;const auto allocations=scenes.ComponentAllocations;
    Check(editor.GetSceneComponentChoices(scope,reference.ComponentType,choices,error)&&choices.size()==2&&scenes.ComponentAllocations==allocations,"Browsing allocated author component identities");
    ScriptValue selected;Check(!editor.ComponentReferenceFromObject(scope,source,reference.ComponentType,selected,error),"Ambiguous component drop silently selected the first match");
    Check(!editor.ReferenceFromComponent(scope,source,first->GetComponentInstanceID(),"GameSproutRuntime.Rigidbody",selected,error),"Wrong component type accepted");
    Check(editor.ReferenceFromComponent(scope,source,second->GetComponentInstanceID(),reference.ComponentType,selected,error)&&selected.Component.ComponentFileID&&scenes.ComponentAllocations==allocations+1,error);
    auto referencePath=listPath;referencePath.push_back({ScriptPathKind::Element,{},0});referencePath.push_back({ScriptPathKind::Member,"Body"});
    Check(editor.SetFieldPath(scope,"Tree",referencePath,selected,error)&&editor.LocateComponentReference(scope,selected,error)&&selection.SceneSelection.ObjectID==target->GetInstanceID(),error);
    Check(editor.ReadFields(owner->GetInstanceID(),2,snapshot,error)&&!snapshot.Fields[0].Children[0].Children[0].Children[1].MissingObject,"Assigned nested component appears missing");
    const auto componentID=second->GetComponentInstanceID();Check(target->RemoveComponentInstance(componentID),"Cannot remove referenced collider");
    Check(editor.ReadFields(owner->GetInstanceID(),2,snapshot,error)&&snapshot.Fields[0].Children[0].Children[0].Children[1].MissingObject,"Removed nested component lacks Missing indication");
    numberValue.Number=21;Check(editor.SetFieldPath(scope,"Tree",numberPath,numberValue,error),"Unrelated member edit erased or rejected an existing Missing reference");
    auto none=selected;none.Component={};Check(editor.SetFieldPath(scope,"Tree",referencePath,none,error),error);
    scenes.Playing=true;mode.State.State=EditorPlayState::Playing;Check(runtime->BeginExecution(error),error);
    auto* transient=scenes.Scene->CreateGameObject("Play body");auto* live=runtime->AddBuiltinComponent(transient,"GameSproutRuntime.Rigidbody",error);Check(live,error);
    const auto beforeLive=scenes.ComponentAllocations;Check(editor.ReadObjectTarget(transient->GetInstanceID(),source,error)&&editor.ComponentReferenceFromObject(scope,source,"GameSproutRuntime.Rigidbody",selected,error),error);
    Check(!selected.Component.ComponentFileID&&selected.Component.RuntimeComponentID==live->GetComponentInstanceID()&&scenes.ComponentAllocations==beforeLive,"Play component reference allocated author FileID");
    Check(runtime->EndExecution(error),error);scenes.Playing=false;mode.State.State=EditorPlayState::Edit;
    Check(runtime->RemoveScriptComponent(owner,2,error),error);
    editor._Types.pop_back();Check(runtime->UnregisterScriptType(type.Info.TypeID,error),error);
}

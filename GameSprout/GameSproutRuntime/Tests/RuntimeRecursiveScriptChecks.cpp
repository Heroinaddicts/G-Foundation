#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {
void Check(bool success,const std::string& error){if(!success)throw std::runtime_error(error);}
int Created=0;
}
struct ReferenceGroup {
private:
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IColliderComponent* _First=nullptr;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IColliderComponent* _Second=nullptr;
public:
    std::vector<GameSproutRuntime::IColliderComponent*> Items;
    std::unordered_map<std::uint64_t,GameSproutRuntime::IColliderComponent*> Map;
    std::uint64_t Large=std::numeric_limits<std::uint64_t>::max();
    GameSproutRuntime::IColliderComponent* First()const{return _First;}
    GameSproutRuntime::IColliderComponent* Second()const{return _Second;}
    void ClearSecond(){_Second=nullptr;}
};
class RecursiveScript final:public GameSproutRuntime::ScriptBehaviour {
public:
    ReferenceGroup Data;
    RecursiveScript* Next=nullptr;
    GameSproutRuntime::IScriptComponent* Wrapper=nullptr;
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*)override {if(Next)Check(Next->Data.Large==std::numeric_limits<std::uint64_t>::max(),"OnRelease observed a dead peer");}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*)override {Check(Next&&Next->Next==this,"Cross-script references not restored before OnCreate");++Created;}
};
namespace GameSproutRuntime::Internal {
ScriptFieldInfo ComponentSchema(const std::string& id,const std::string& type="GameSproutRuntime.Collider"){
    ScriptFieldInfo schema;schema.ID=id;schema.Name=id;schema.Default.Kind=ScriptValueKind::Component;schema.ComponentType=type;return schema;
}
ScriptFieldInfo GroupSchema(){
    ScriptFieldInfo schema;schema.Default.Kind=ScriptValueKind::Structure;
    auto first=ComponentSchema("first"),second=ComponentSchema("second");
    ScriptFieldInfo items;items.ID="items";items.Name="Items";items.Default.Kind=ScriptValueKind::Sequence;items.Children={ComponentSchema("element")};
    ScriptFieldInfo key;key.ID="key";key.Default.Kind=ScriptValueKind::Unsigned;
    ScriptFieldInfo map;map.ID="map";map.Name="Map";map.Default.Kind=ScriptValueKind::Dictionary;map.Children={key,ComponentSchema("value")};
    ScriptFieldInfo large;large.ID="large";large.Name="Large";large.Default.Kind=ScriptValueKind::Unsigned;large.Default.Unsigned=std::numeric_limits<std::uint64_t>::max();
    schema.Children={first,second,items,map,large};for(const auto& child:schema.Children)schema.Default.Fields.emplace(child.ID,child.Default);return schema;
}
template<>struct ScriptFieldAccess<ReferenceGroup>{
    static bool Read(const ReferenceGroup& input,ScriptValue& value,const ScriptReferenceContext& context,std::string& error){
        value.Kind=ScriptValueKind::Structure;value.Fields.clear();
        return ReadNativeValue(input._First,value.Fields["first"],context,error)&&ReadNativeValue(input._Second,value.Fields["second"],context,error)&&ReadNativeValue(input.Items,value.Fields["items"],context,error)&&ReadNativeValue(input.Map,value.Fields["map"],context,error)&&ReadNativeValue(input.Large,value.Fields["large"],context,error);
    }
    static bool Validate(const ScriptValue& value,const ScriptReferenceContext& context,std::string& error){
        if(value.Fields.size()!=5){error="Wrong record shape";return false;}
        return ValidateNativeValue<IColliderComponent*>(value.Fields.at("first"),context,error)&&ValidateNativeValue<IColliderComponent*>(value.Fields.at("second"),context,error)&&ValidateNativeValue<std::vector<IColliderComponent*>>(value.Fields.at("items"),context,error)&&ValidateNativeValue<std::unordered_map<std::uint64_t,IColliderComponent*>>(value.Fields.at("map"),context,error)&&ValidateNativeValue<std::uint64_t>(value.Fields.at("large"),context,error);
    }
    static bool Write(ReferenceGroup& output,const ScriptValue& value,const ScriptReferenceContext& context,std::string& error){
        return WriteNativeValue(output._First,value.Fields.at("first"),context,error)&&WriteNativeValue(output._Second,value.Fields.at("second"),context,error)&&WriteNativeValue(output.Items,value.Fields.at("items"),context,error)&&WriteNativeValue(output.Map,value.Fields.at("map"),context,error)&&WriteNativeValue(output.Large,value.Fields.at("large"),context,error);
    }
};
}
int main(int argc,char** argv){
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        using namespace GameSproutRuntime;
        Check(argc==2,"Missing isolated Runtime path");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");std::string error;
        int fixed[3]={1,2,3};ScriptValue fixedValue;fixedValue.Kind=ScriptValueKind::Sequence;
        for(int integer:{4,5,6}){ScriptValue item;item.Kind=ScriptValueKind::Integer;item.Integer=integer;fixedValue.Elements.push_back(item);}
        Check(Internal::WriteNativeValue(fixed,fixedValue,{},error)&&fixed[0]==4&&fixed[2]==6,error);
        fixedValue.Elements[2].Integer=std::numeric_limits<std::int64_t>::max();Check(!Internal::WriteNativeValue(fixed,fixedValue,{},error)&&fixed[0]==4&&fixed[2]==6,"Rejected fixed-array edit partially changed its values");
        int matrix[2][3]={{1,2,3},{4,5,6}};ScriptValue matrixValue;
        Check(Internal::ReadNativeValue(matrix,matrixValue,{},error),error);
        matrixValue.Elements[0].Elements[0].Integer=7;matrixValue.Elements[1].Elements[2].Integer=9;
        Check(Internal::WriteNativeValue(matrix,matrixValue,{},error)&&matrix[0][0]==7&&matrix[1][2]==9,error);
        matrixValue.Elements[0].Elements[0].Integer=100;matrixValue.Elements[1].Elements[2].Integer=std::numeric_limits<std::int64_t>::max();
        Check(!Internal::WriteNativeValue(matrix,matrixValue,{},error)&&matrix[0][0]==7&&matrix[1][2]==9,"Rejected 2D array edit partially wrote an earlier leaf");
        int cube[2][2][2]={{{1,2},{3,4}},{{5,6},{7,8}}};ScriptValue cubeValue;
        Check(Internal::ReadNativeValue(cube,cubeValue,{},error),error);
        cubeValue.Elements[0].Elements[0].Elements[0].Integer=11;cubeValue.Elements[1].Elements[1].Elements[1].Integer=18;
        Check(Internal::WriteNativeValue(cube,cubeValue,{},error)&&cube[0][0][0]==11&&cube[1][1][1]==18,error);
        cubeValue.Elements[0].Elements[0].Elements[0].Integer=200;cubeValue.Elements[1].Elements[1].Elements[1].Integer=std::numeric_limits<std::int64_t>::max();
        Check(!Internal::WriteNativeValue(cube,cubeValue,{},error)&&cube[0][0][0]==11&&cube[1][1][1]==18,"Rejected 3D array edit partially wrote an earlier leaf");
        std::array<int,0> empty;ScriptValue emptyValue;Check(Internal::ReadNativeValue(empty,emptyValue,{},error)&&emptyValue.Elements.empty()&&Internal::WriteNativeValue(empty,emptyValue,{},error),error);
        std::vector<bool> booleans={true,false};ScriptValue booleanValue;Check(Internal::ReadNativeValue(booleans,booleanValue,{},error)&&booleanValue.Elements[0].Boolean&&!booleanValue.Elements[1].Boolean,error);
        const auto group=Internal::GroupSchema();auto next=Internal::ComponentSchema("next","fedcba9876543210fedcba9876543210");
        Internal::RegisterScriptClass<RecursiveScript>("fedcba9876543210fedcba9876543210","Recursive","Recursive.hpp",{"0123456789abcdef0123456789abcdef",1},{Internal::MakeField<RecursiveScript>("data","Data",&RecursiveScript::Data,group),Internal::MakeField<RecursiveScript>("next","Next",&RecursiveScript::Next,next),Internal::MakeField<RecursiveScript>("wrapper","Wrapper",&RecursiveScript::Wrapper,Internal::ComponentSchema("wrapper","GameSproutRuntime.ScriptComponent"))});
        Check(runtime->RegisterScriptType(Internal::ScriptModuleTypes().back(),error),error);
        auto* scene=runtime->CreateScene("Recursive");auto* a=scene->CreateGameObject("A");auto* b=scene->CreateGameObject("B");auto* target=scene->CreateGameObject("Target");
        auto* first=target->AddComponent<IBoxColliderComponent>();auto* second=target->AddComponent<IBoxColliderComponent>();auto* body=target->AddComponent<IRigidbodyComponent>();
        Check(first&&second&&first!=second&&body,"Missing multiple native components");
        auto* sa=runtime->AddScriptComponent(a,"fedcba9876543210fedcba9876543210",{"0123456789abcdef0123456789abcdef",1},0,error);auto* sb=runtime->AddScriptComponent(b,"fedcba9876543210fedcba9876543210",{"0123456789abcdef0123456789abcdef",1},0,error);Check(sa&&sb,error);
        Check(scene->BindObjectIdentities({{scene->GetInstanceID(),0},{a->GetInstanceID(),1},{b->GetInstanceID(),2},{target->GetInstanceID(),3}},error),error);
        Check(scene->SetComponentIdentities({{first,10},{second,11},{body,12},{sa,20},{sb,21}},error),error);
        ScriptValue ref;ref.Kind=ScriptValueKind::Component;ref.Component={true,10};
        auto value=group.Default;value.Fields["first"]=ref;ref.Component.ComponentFileID=11;value.Fields["second"]=ref;
        value.Fields["items"].Elements={value.Fields["first"],value.Fields["second"]};
        ScriptValue smallKey;smallKey.Kind=ScriptValueKind::Unsigned;smallKey.Unsigned=std::numeric_limits<std::uint64_t>::max()-1;
        ScriptValue largeKey=smallKey;largeKey.Unsigned=std::numeric_limits<std::uint64_t>::max();
        value.Fields["map"].Entries={{largeKey,value.Fields["first"]},{smallKey,value.Fields["second"]}};
        Check(sa->SetField("data",value,error),error);
        ScriptValue nextValue;nextValue.Kind=ScriptValueKind::Component;nextValue.Component={true,21};Check(sa->SetField("next",nextValue,error),error);nextValue.Component.ComponentFileID=20;Check(sb->SetField("next",nextValue,error),error);
        nextValue.Component.ComponentFileID=21;Check(sa->SetField("wrapper",nextValue,error),error);
        auto invalid=value;invalid.Fields["first"].Component.ComponentFileID=12;Check(!sa->SetField("data",invalid,error),"Wrong component type was accepted in Edit");Check(sa->GetFields().at("data")==value,"Rejected edit changed sibling fields");
        invalid=value;invalid.Fields["large"].Kind=ScriptValueKind::Integer;Check(!sa->SetField("data",invalid,error),"Wrong nested kind accepted");
        Check(Created==0,"Edit instantiated native gameplay scripts");Check(runtime->BeginExecution(error),error);
        auto* native=a->GetComponent<RecursiveScript>();Check(native&&native->Next==b->GetComponent<RecursiveScript>()&&Created==2,"Two-phase native reference startup failed");
        Check(native->Data.First()==first&&native->Data.Second()==second&&native->Data.Items[0]==first&&native->Data.Map.at(largeKey.Unsigned)==first&&native->Data.Large==std::numeric_limits<std::uint64_t>::max(),"Recursive data lost native fields");
        std::map<std::string,ScriptValue> snapshot;Check(sa->ReadFields(snapshot,error),error);const auto& entries=snapshot.at("data").Fields.at("map").Entries;Check(entries[0].first.Unsigned==smallKey.Unsigned&&entries[1].first.Unsigned==largeKey.Unsigned,"Unordered dictionary ordering rounded uint64 keys");
        native->Data.ClearSecond();
        Check(target->RemoveComponentInstance(first->GetComponentInstanceID()),"Cannot remove first same-type component");
        Check(!native->Data.First()&&!native->Data.Second()&&!native->Data.Items[0]&&native->Data.Items[1]==second&&!native->Data.Map.at(largeKey.Unsigned),"Removal did not invalidate recursive references precisely");
        Check(sa->GetFields().at("data").Fields.at("first").Component.ComponentFileID==10,"Removal lost persistent Missing identity");
        auto* restored=target->AddComponent<IBoxColliderComponent>();Check(restored&&scene->SetComponentIdentities({{restored,10},{second,11},{body,12},{sa,20},{sb,21}},error),error);
        Check(native->Data.First()==restored&&!native->Data.Second()&&native->Data.Items[0]==restored&&native->Data.Items[1]==second&&native->Data.Map.at(largeKey.Unsigned)==restored,"Restore clobbered another leaf or failed nested rebind");
        sb->SetEnabled(false);
        Check(!native->Next&&native->Wrapper==sb,"Stopping native peer left a dangling pointer or cleared its live wrapper");
        sb->SetEnabled(true);runtime->Update(.01f);
        Check(native->Next&&native->Next==b->GetComponent<RecursiveScript>()&&native->Wrapper==sb,"Restarting peer did not restore the typed native reference: A="+sa->GetScriptError()+" B="+sb->GetScriptError()+" created="+std::to_string(Created)+" next="+std::to_string(native->Next!=nullptr)+" b="+std::to_string(b->GetComponent<RecursiveScript>()!=nullptr));
        Check(runtime->EndExecution(error),error);Check(runtime->DestroyScene(scene),"Cannot destroy scene");delete runtime;runtime=nullptr;
        std::cout<<"Recursive script fields and component identities passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}

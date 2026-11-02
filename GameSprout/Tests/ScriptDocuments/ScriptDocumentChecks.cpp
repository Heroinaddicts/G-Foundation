#include "SceneDocument.h"
#include "PrefabDocument.h"
#include "ScriptValueDocument.h"
#include "ScriptReferenceDocument.h"
#include "PrefabDefinition.h"
#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool valid,const std::string& message){if(!valid)throw std::runtime_error(message);}
GameSproutRuntime::ScriptValue Integer(std::int64_t value) {
    GameSproutRuntime::ScriptValue result;result.Kind=GameSproutRuntime::ScriptValueKind::Integer;result.Integer=value;return result;
}
GameSproutRuntime::ScriptValue Tree() {
    using namespace GameSproutRuntime;
    ScriptValue record;record.Kind=ScriptValueKind::Structure;
    record.Fields["Record::Signed"]=Integer(std::numeric_limits<std::int64_t>::min());
    auto& maximum=record.Fields["Record::Unsigned"];maximum.Kind=ScriptValueKind::Unsigned;maximum.Unsigned=std::numeric_limits<std::uint64_t>::max();
    auto& sequence=record.Fields["Record::References"];sequence.Kind=ScriptValueKind::Sequence;
    ScriptValue reference;reference.Kind=ScriptValueKind::Component;reference.Component.Assigned=true;reference.Component.ComponentFileID=7;
    sequence.Elements.push_back(reference);
    auto& dictionary=record.Fields["Record::Dictionary"];dictionary.Kind=ScriptValueKind::Dictionary;
    ScriptValue key;key.Kind=ScriptValueKind::String;key.String="目标";
    ScriptValue target;target.Kind=ScriptValueKind::SceneObject;target.SceneObject={true,5};
    dictionary.Entries.emplace_back(key,target);
    return record;
}
Json::Value Document() {
    auto data=SceneDocument::CreateDefault("Recursive");auto& root=data["Objects"][0];
    root["Components"][0]["ID"]=Json::UInt64(2);root["Components"][1]["ID"]=Json::UInt64(3);
    Json::Value script(Json::objectValue);script["ID"]=Json::UInt64(4);script["Type"]="GameSproutRuntime.ScriptComponent";
    script["InstanceID"]=Json::UInt64(1);script["ScriptTypeID"]="00112233445566778899aabbccddeeff";
    script["Source"]["Guid"]="00112233445566778899aabbccddeeff";script["Source"]["LocalID"]=Json::UInt64(1);script["Enabled"]=true;
    script["Fields"]["Script::Record"]=ScriptValueDocument::Serialize(Tree());root["Components"].append(script);
    auto child=root;child["ID"]=Json::UInt64(5);child["ParentID"]=Json::UInt64(1);child["Name"]="Target";
    child["Components"]=Json::Value(Json::arrayValue);child["Components"].append(root["Components"][0]);child["Components"][0]["ID"]=Json::UInt64(6);
    Json::Value camera=root["Components"][1];camera["ID"]=Json::UInt64(7);child["Components"].append(camera);
    data["Objects"].append(child);data["NextObjectID"]=Json::UInt64(8);return data;
}
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Runtime directory required");auto tree=Tree();const auto encoded=ScriptValueDocument::Serialize(tree);
        Check(ScriptValueDocument::Parse(encoded)==tree,"Recursive round trip lost a value or uint64 precision");
        auto ephemeral=tree;ephemeral.Fields["Record::References"].Elements[0].Component.RuntimeSceneID=9;
        bool rejected=false;try {ScriptValueDocument::Serialize(ephemeral);}catch(const std::exception&){rejected=true;}
        Check(rejected,"A nested session component reference escaped into persistent data");
        auto malformed=encoded;malformed["Value"]["Record::References"]["Value"][0]["Value"]["RuntimeObjectID"]=1;
        rejected=false;try {ScriptValueDocument::Parse(malformed);}catch(const std::exception&){rejected=true;}
        Check(rejected,"Unknown nested reference field accepted");
        GameSproutRuntime::ScriptValue dictionary;dictionary.Kind=GameSproutRuntime::ScriptValueKind::Dictionary;
        GameSproutRuntime::ScriptValue keyA;keyA.Kind=GameSproutRuntime::ScriptValueKind::String;keyA.String="a";
        auto keyB=keyA;keyB.String="b";auto keyC=keyA;keyC.String="c";
        dictionary.Entries={{keyA,Integer(1)},{keyB,Integer(2)}};
        const auto baseline=ScriptValueDocument::Serialize(dictionary);
        auto instance=dictionary;instance.Entries[0].second=Integer(10);
        std::vector<PrefabDefinition::Patch> patches;
        PrefabDefinition::DiffValues(baseline,ScriptValueDocument::Serialize(instance),{},1,1,patches);
        auto source=dictionary;source.Entries={{keyC,Integer(3)},{keyB,Integer(2)},{keyA,Integer(1)}};
        auto sourceData=ScriptValueDocument::Serialize(source);
        for(const auto& patch:patches)PrefabDefinition::SetPath(sourceData,patch);
        for(const auto& patch:patches) {
            const auto* retained=PrefabDefinition::FindPath(sourceData,patch.Path);
            Check(retained&&PrefabDefinition::Equivalent(*retained,patch.After),"Stored dictionary override could not be located by key");
        }
        const auto mergedDictionary=ScriptValueDocument::Parse(sourceData);
        Check(mergedDictionary.Entries[0].second.Integer==3&&mergedDictionary.Entries[1].second.Integer==2&&mergedDictionary.Entries[2].second.Integer==10,
            "Dictionary Prefab override followed an array index instead of its key");
        auto document=Document();std::string error;Check(PrefabDocument::Validate(document,error),error);
        auto remapped=document;Check(PrefabDocument::Remap(remapped,{{1,11},{2,12},{3,13},{4,14},{5,15},{6,16},{7,17}},error),error);
        auto fields=ScriptValueDocument::Parse(remapped["Objects"][0]["Components"][2]["Fields"]["Script::Record"]);
        Check(fields.Fields.at("Record::References").Elements[0].Component.ComponentFileID==17,"Nested component reference not remapped");
        Check(fields.Fields.at("Record::Dictionary").Entries[0].second.SceneObject.ObjectFileID==15,"Dictionary object reference not remapped");
        auto canonical=PrefabDefinition::Canonicalize(remapped);Check(PrefabDocument::Validate(canonical,error),error);
        runtime=Export(argv[1]);Check(runtime,"Runtime load");
        GameSproutRuntime::IScene* scene=nullptr;SceneDocument::ObjectIDs ids;
        Check(SceneDocument::Instantiate(document,*runtime,scene,ids,error),error);
        auto* referenced=runtime->QuerySceneComponentByFileID(scene,7);Check(referenced&&referenced->GetGameObject()->GetName()=="Target","Document did not publish component identity");
        Json::Value saved;Check(SceneDocument::Serialize(*scene,ids,saved,error),error);
        Check(saved["Objects"][0]["Components"][2]["Fields"]==document["Objects"][0]["Components"][2]["Fields"],"Scene save changed recursive authored fields");
        GameSproutRuntime::IGameObject *first=nullptr,*second=nullptr;std::vector<GameSproutRuntime::PrefabObjectMapping> mapping;
        Check(SceneDocument::InstantiatePrefab(document,*runtime,*scene,*scene,first,mapping,error),error);
        Check(SceneDocument::InstantiatePrefab(document,*runtime,*scene,*scene,second,mapping,error),error);
        auto* firstScript=dynamic_cast<GameSproutRuntime::IScriptComponent*>(first->GetComponentAt(2));
        auto* secondScript=dynamic_cast<GameSproutRuntime::IScriptComponent*>(second->GetComponentAt(2));
        Check(firstScript&&secondScript,"Prefab script components missing");
        const auto firstRef=firstScript->GetFields().at("Script::Record").Fields.at("Record::References").Elements[0].Component;
        const auto secondRef=secondScript->GetFields().at("Script::Record").Fields.at("Record::References").Elements[0].Component;
        Check(firstRef.ComponentFileID!=secondRef.ComponentFileID,"Prefab instances share component FileIDs");
        Check(runtime->QuerySceneComponentByFileID(scene,firstRef.ComponentFileID)->GetGameObject()->Parent()==first,"First Prefab reference escaped to another instance");
        Check(runtime->QuerySceneComponentByFileID(scene,secondRef.ComponentFileID)->GetGameObject()->Parent()==second,"Second Prefab reference escaped to another instance");
        delete runtime;runtime=nullptr;std::cout<<"PASS recursive values, uint64, transient rejection, Prefab remap, scene identities and separate-instance references\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}

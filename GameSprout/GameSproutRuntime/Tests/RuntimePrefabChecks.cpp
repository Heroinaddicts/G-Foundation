#include "GameSproutRuntimeExport.h"
#include "RuntimeResourceApi.h"
#include "Internal/RuntimeScriptBindings.h"
#include "PrefabDocument.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
int Created=0,Initialized=0,Released=0;
bool FailInitialization=false;
const GameSproutRuntime::ResourceReference Source{"0123456789abcdef0123456789abcdef",1};
}
class PrefabProbe final:public GameSproutRuntime::ScriptBehaviour {
public:
    GameSproutRuntime::IGameObject* Target=nullptr;
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*)override {
        ++Created;
        Check(Target&&Target==_GameObject->FindChild("Target"),"Prefab OnCreate has an unresolved/cross-instance reference");
        Check(Target->GetTransform()->GetLocalPosition().X==3,"Prefab OnCreate ran before authored child properties");
        Check(_GameObject->GetComponent<GameSproutRuntime::IImageComponent>(),"Prefab OnCreate ran before all components existed");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*)override {
        ++Initialized;
        auto* animator=_GameObject->GetComponent<GameSproutRuntime::IAnimatorComponent>();
        auto* skin=Target->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
        Check(animator&&animator->GetNodeBindings().size()==1&&animator->GetNodeBindings()[0].ObjectID==Target->GetInstanceID(),"Prefab Animator binding was not remapped");
        Check(skin&&skin->GetAnimatorObjectID()==_GameObject->GetInstanceID(),"Prefab skin references another instance");
        if(FailInitialization)throw std::runtime_error("Intentional Prefab initialization failure");
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*)override {++Released;}
};
namespace {
Json::Value Component(const char* name,std::uint64_t id) {
    Json::Value result(Json::objectValue);result["Type"]=std::string("GameSproutRuntime.")+name;result["ID"]=Json::UInt64(id);return result;
}
Json::Value Template() {
    Json::Value data(Json::objectValue);data["Version"]=GameSproutRuntime::SceneDocumentVersion;data["Name"]="Prefab";data["NextObjectID"]=Json::UInt64(200);
    data["Objects"]=Json::Value(Json::arrayValue);
    Json::Value root(Json::objectValue);root["ID"]=Json::UInt64(10);root["ParentID"]=Json::UInt64(0);root["Name"]="Root";root["Components"]=Json::Value(Json::arrayValue);
    auto script=Component("ScriptComponent",100);script["InstanceID"]=Json::UInt64(7);script["ScriptTypeID"]="prefab-probe";
    script["Source"]["Guid"]=Source.Guid;script["Source"]["LocalID"]=Json::UInt64(Source.LocalID);script["Enabled"]=true;
    script["Fields"]["target"]["Kind"]="SceneObject";script["Fields"]["target"]["Value"]["ObjectFileID"]=Json::UInt64(20);
    root["Components"].append(script);root["Components"].append(Component("Image",101));
    auto animator=Component("Animator",102);animator["Speed"]=1;animator["PlayOnStart"]=false;animator["Loop"]=true;
    animator["Bindings"]=Json::Value(Json::arrayValue);Json::Value binding;binding["Path"]="Target";binding["ObjectFileID"]=Json::UInt64(20);animator["Bindings"].append(binding);
    root["Components"].append(animator);data["Objects"].append(root);
    Json::Value target(Json::objectValue);target["ID"]=Json::UInt64(20);target["ParentID"]=Json::UInt64(10);target["Name"]="Target";target["Components"]=Json::Value(Json::arrayValue);
    auto transform=Component("Transform",103);transform["Position"]=Json::Value(Json::arrayValue);transform["Position"].append(3);transform["Position"].append(0);transform["Position"].append(0);target["Components"].append(transform);
    auto skin=Component("SkinnedMeshRenderer",104);skin["AnimatorObjectFileID"]=Json::UInt64(10);target["Components"].append(skin);data["Objects"].append(target);
    return data;
}
class Provider final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    GameSproutRuntime::PrefabData Data;
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&)override{return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
    bool LoadPrefab(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::PrefabData& output,std::string& error)override {output=Data;error.clear();return true;}
};
}
int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;Provider provider;
    try {
        Check(argc==2,"Runtime directory required");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        GameSproutRuntime::Internal::RegisterScriptClass<PrefabProbe>("prefab-probe","PrefabProbe","PrefabProbe.hpp",Source,
            {GameSproutRuntime::Internal::MakeField<PrefabProbe>("target","Target",&PrefabProbe::Target,static_cast<GameSproutRuntime::IGameObject*>(nullptr))});
        std::string error;Check(runtime->RegisterScriptType(GameSproutRuntime::Internal::ScriptModuleTypes().back(),error),error);
        auto document=Template();Check(PrefabDocument::Encode(document,provider.Data.Bytes,error),error);
        Json::Value decoded;Check(PrefabDocument::Decode(provider.Data.Bytes,decoded,error)&&decoded==document,"Prepared Prefab round trip changed data");
        GameSproutRuntime::IScene* authored=nullptr;SceneDocument::ObjectIDs authoredIDs;
        Check(SceneDocument::Instantiate(document,*runtime,authored,authoredIDs,error),error);
        auto* authoredRoot=authored->FindChild("Root");auto* wrapper=authoredRoot->GetComponent<GameSproutRuntime::IScriptComponent>();
        auto originalScript=document["Objects"][0]["Components"][0],emptyScript=originalScript;
        emptyScript["Fields"]=Json::Value(Json::objectValue);
        Check(SceneDocument::RestoreComponent(emptyScript,*authoredRoot,*runtime,error)&&wrapper==authoredRoot->GetComponent<GameSproutRuntime::IScriptComponent>()&&
              wrapper->GetFields().size()==1&&!wrapper->GetFields().at("target").SceneObject.Assigned,"Script document restoration did not use current missing-field defaults");
        Check(SceneDocument::RestoreComponent(originalScript,*authoredRoot,*runtime,error)&&wrapper->GetFields().size()==1,"Script document patch cannot restore removed fields");
        auto invalidScript=originalScript;invalidScript["Fields"]["unknown"]=invalidScript["Fields"]["target"];
        Check(SceneDocument::RestoreComponent(invalidScript,*authoredRoot,*runtime,error)&&wrapper->GetFields().size()==1&&wrapper->GetFields().count("target"),"Script document restoration did not discard removed fields");
        auto strictFields=wrapper->GetFields();strictFields.emplace("unknown",strictFields.at("target"));
        Check(!wrapper->ReplaceFields(strictFields,error)&&wrapper->GetFields().size()==1,"Strict script field replacement accepted an undeclared field");
        Json::Value saved;Check(SceneDocument::Serialize(*authored,authoredIDs,saved,error),error);
        Check(saved["Objects"][0]["Components"][1]["ID"]==Json::Value(Json::UInt64(100)),"Loaded component stable ID was not preserved");
        GameSproutRuntime::IScene* reopened=nullptr;SceneDocument::ObjectIDs reopenedIDs;
        Check(SceneDocument::Instantiate(saved,*runtime,reopened,reopenedIDs,error),error);
        Json::Value savedAgain;Check(SceneDocument::Serialize(*reopened,reopenedIDs,savedAgain,error)&&savedAgain==saved,"Component IDs changed on object-document round trip");
        Check(runtime->DestroyScene(authored)&&runtime->DestroyScene(reopened),"Cannot release authoring fixture scenes");
        auto* scene=runtime->CreateScene("Target scene");auto* outside=scene->CreateGameObject("Outside");Check(scene->BindObjectIdentity(outside,20,error),error);
        auto* mainCamera=outside->AddComponent<GameSproutRuntime::ICameraComponent>();Check(scene->SetMainCamera(mainCamera),"Cannot configure existing scene camera");
        runtime->SetResourceProvider(&provider);Check(runtime->BeginExecution(error),error);
        std::vector<GameSproutRuntime::PrefabObjectMapping> firstMap,secondMap;
        auto* first=runtime->InstantiatePrefabData(scene,provider.Data,nullptr,firstMap,error);Check(first,error);
        auto* second=runtime->InstantiatePrefab(scene,Source,nullptr,secondMap,error);Check(second,error);
        Check(firstMap.size()==2&&secondMap.size()==2&&firstMap[0].SourceID==10&&firstMap[0].ObjectID==first->GetInstanceID(),"Prefab mapping lost source identities");
        auto* firstProbe=first->GetComponent<PrefabProbe>();auto* secondProbe=second->GetComponent<PrefabProbe>();
        Check(firstProbe&&secondProbe&&firstProbe->Target!=secondProbe->Target&&Created==2&&Initialized==2,"Multiple Prefabs share an internal reference/lifecycle");
        std::uint64_t firstFile=0,secondFile=0;Check(runtime->GetSceneObjectFileID(firstProbe->Target,firstFile)&&runtime->GetSceneObjectFileID(secondProbe->Target,secondFile)&&firstFile!=secondFile&&firstFile!=20&&secondFile!=20,"Prefab retained template FileIDs/collided with existing scene identities");
        const auto count=scene->GetChildCount();std::vector<GameSproutRuntime::PrefabObjectMapping> unchanged{{999,888}};
        FailInitialization=true;Check(!runtime->InstantiatePrefabData(scene,provider.Data,nullptr,unchanged,error),"Failing initialization succeeded");
        Check(scene->GetChildCount()==count&&scene->GetMainCamera()==mainCamera&&unchanged.size()==1&&unchanged[0].SourceID==999,"Failed Prefab leaked nodes, changed main camera or mutated mapping output");
        FailInitialization=false;
        auto broken=provider.Data;broken.Bytes.pop_back();Check(!runtime->InstantiatePrefabData(scene,broken,nullptr,unchanged,error)&&scene->GetChildCount()==count,"Truncated Prefab mutated scene");
        broken=provider.Data;broken.Bytes.push_back(0);Check(!runtime->InstantiatePrefabData(scene,broken,nullptr,unchanged,error),"Prefab accepted trailing bytes");
        auto duplicate=document;duplicate["Objects"][1]["Components"][0]["ID"]=Json::UInt64(10);
        Check(!PrefabDocument::Encode(duplicate,broken.Bytes,error),"Component and object IDs collided in one document");
        auto* otherScene=runtime->CreateScene("Other scene");
        Check(!runtime->InstantiatePrefabData(scene,provider.Data,otherScene,unchanged,error)&&scene->GetChildCount()==count,"Prefab accepted a parent from another scene");
        Check(runtime->DestroyScene(otherScene),"Cannot release parent validation fixture");
        document["Objects"][0]["Components"][0]["Fields"]["target"]["Value"]["ObjectFileID"]=Json::UInt64(999);
        Check(!PrefabDocument::Encode(document,broken.Bytes,error),"Prefab accepted external scene object reference");
        Check(scene->DestroyGameObject(first),"Cannot destroy first Prefab");
        Check(secondProbe->Target==second->FindChild("Target"),"Destroying one Prefab invalidated another instance");
        Check(runtime->EndExecution(error),error);runtime->SetResourceProvider(nullptr);delete runtime;runtime=nullptr;
        Check(Released==3,"Prefab script lifecycle did not release every created instance exactly once");
        std::cout<<"Prefab assembly, reference isolation, lifecycle and rollback checks passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';delete runtime;return 1;}
}

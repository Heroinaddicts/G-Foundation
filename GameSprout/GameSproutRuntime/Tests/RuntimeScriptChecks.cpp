#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <iostream>
#include <stdexcept>

static void Check(bool condition,const char* error) {if(!condition) throw std::runtime_error(error);}
#include "RuntimeScriptFieldRestoreChecks.h"
class Events : public GameSproutRuntime::IRuntimeListener {
public:
    std::string Error;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::OperationFailed) Error=event.Error;
    }
};
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    auto* runtime=Export(argv[1]);
    try {
        Check(runtime,"Cannot load Runtime");
        CheckSerializedFieldEvolution(*runtime);
#if defined(_WIN32)
        auto module=LoadLibraryW(std::filesystem::path(argv[2]).c_str());
        auto entry=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(GetProcAddress(module,GameSproutRuntime::ScriptModuleEntryName));
        auto counts=reinterpret_cast<const int*(*)()>(GetProcAddress(module,"GetNativeScriptCounts"));
        auto cascade=reinterpret_cast<bool(*)(GameSproutRuntime::IGameObject*)>(GetProcAddress(module,"CheckNativeCascadeRemoval"));
#else
        void* module=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);
        if(!module) throw std::runtime_error(dlerror());
        auto entry=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(dlsym(module,GameSproutRuntime::ScriptModuleEntryName));
        auto counts=reinterpret_cast<const int*(*)()>(dlsym(module,"GetNativeScriptCounts"));
        auto cascade=reinterpret_cast<bool(*)(GameSproutRuntime::IGameObject*)>(dlsym(module,"CheckNativeCascadeRemoval"));
#endif
        Check(entry&&counts&&cascade,"Missing GameLogic native exports");
        const auto* api=entry();Check(api&&api->Version==GameSproutRuntime::ScriptModuleABIVersion&&api->Count==4,"Invalid compiled type catalog");
        std::string error;
        Check(runtime->RegisterScriptType(api->Types[0],error),error.c_str());
        Check(!runtime->RegisterScriptType(api->Types[0],error),"Duplicate script type accepted");
        for(std::size_t i=1;i<api->Count;++i)Check(runtime->RegisterScriptType(api->Types[i],error),error.c_str());
        auto* scene=runtime->CreateScene("Scene");auto* object=scene->CreateGameObject("Mover");
        const GameSproutRuntime::ResourceReference source=api->Types[0].Source;
        auto* script=runtime->AddScriptComponent(object,api->Types[0].Info.TypeID,source,0,error);
        Check(script&&script->IsScriptTypeAvailable(),"Cannot attach compiled script");
        Check(script->SetField("Speed",GameSproutRuntime::Internal::ScriptValueOf(3.0f),error),"Cannot author script field");
        {
            auto candidate=api->Types[0];
            auto added=candidate.Fields.front();added.Info.ID="newField";candidate.Fields.push_back(added);
            auto invalid=candidate;invalid.Info.TypeID="invalid";invalid.Fields.front().Write={};
            const auto authored=script->GetFields();
            Check(!runtime->ReplaceScriptTypes({candidate.Info.TypeID},{candidate,invalid},error),"Invalid native module was published");
            Check(script->GetFields()==authored&&runtime->GetScriptTypes().size()==4,"Rejected native module polluted authored fields or catalog");
        } // All copied DLL-owned closures must die before dlclose below.
        Check(!script->SetField("Speed",GameSproutRuntime::Internal::ScriptValueOf(std::string("wrong")),error),"Mismatched field accepted");
        Check(script->SetField("AddObserver",GameSproutRuntime::Internal::ScriptValueOf(true),error),"Cannot configure nested type creation");
        runtime->Update(1);
        Check(counts()[0]==0&&object->GetTransform()->GetLocalPosition().X==0,"Editor evaluation executed user code");
        Check(runtime->SetScriptExecutionEnabled(true,error),error.c_str());runtime->Update(0);
        Check(counts()[0]==0,"Paused Update(0) executed user lifecycle");
        runtime->Update(0.5f);
        Check(counts()[0]==1&&counts()[1]==1&&object->GetTransform()->GetLocalPosition().X==1.5f&&object->GetTransform()->GetLocalPosition().Y==3,"Native lifecycle or field restore failed");
        Check(counts()[4]==1&&counts()[5]==1&&counts()[6]==1,"Native OnCreate did not reject self-add, return this or safely create a different script type");
        Check(counts()[7]==0,"Update unexpectedly ran FixedUpdate");
        runtime->FixedUpdate(.02f);
        Check(counts()[7]==1&&object->GetTransform()->GetLocalPosition().Z>.059f,"Explicit FixedUpdate did not invoke script lifecycle");
        const auto beforeDuplicate=object->GetComponentCount();
        Check(!runtime->AddScriptComponent(object,api->Types[0].Info.TypeID,source,0,error)&&object->GetComponentCount()==beforeDuplicate,"Duplicate native script type accepted");
        Check(runtime->SetScriptExecutionEnabled(false,error),error.c_str());
        Check(counts()[2]==1&&counts()[3]==1,"Stop did not release native instance");
        Check(runtime->UnregisterScriptType(api->Types[0].Info.TypeID,error),error.c_str());
        Check(!script->IsScriptTypeAvailable()&&script->GetFields().at("Speed").Number==3,"Missing type lost authored fields");
        Check(runtime->RegisterScriptType(api->Types[0],error),error.c_str());
        script->SetEnabled(false);
        auto* selfDisableObject=scene->CreateGameObject("Self disable");
        auto* selfDisabling=runtime->AddScriptComponent(selfDisableObject,api->Types[0].Info.TypeID,source,0,error);
        Check(selfDisabling&&selfDisabling->SetField("DisableOnCreate",GameSproutRuntime::Internal::ScriptValueOf(true),error),"Cannot configure self-disabling script");
        const int initialized=counts()[1],released=counts()[2],destroyed=counts()[3];
        Check(runtime->SetScriptExecutionEnabled(true,error),error.c_str());runtime->Update(0.1f);
        Check(!selfDisabling->IsEnabled()&&counts()[1]==initialized&&counts()[2]==released+1&&counts()[3]==destroyed+1,"Self-disable initialized or destroyed the native instance inside its callback");
        Check(counts()[10]==1,"Self-disable callback did not return before destruction");
        runtime->SetScriptExecutionEnabled(false,error);script->SetEnabled(true);
        Events events;runtime->RegisterListener(&events);
        Check(script->SetField("Throw",GameSproutRuntime::Internal::ScriptValueOf(true),error),"Cannot set exception fixture");
        Check(runtime->SetScriptExecutionEnabled(true,error),error.c_str());runtime->Update(0.1f);
        Check(events.Error=="Fixture script error"&&!script->GetScriptError().empty(),"Script error was swallowed");
        runtime->SetScriptExecutionEnabled(false,error);runtime->UnregisterListener(&events);
        auto* removeObject=scene->CreateGameObject("Self remove");
        auto* selfRemoving=runtime->AddScriptComponent(removeObject,api->Types[0].Info.TypeID,source,0,error);
        Check(selfRemoving&&selfRemoving->SetField("RemoveSelf",GameSproutRuntime::Internal::ScriptValueOf(true),error),"Cannot configure self removal");
        script->SetEnabled(false);const int releaseBeforeRemove=counts()[2],destroyBeforeRemove=counts()[3];
        Check(runtime->SetScriptExecutionEnabled(true,error),error.c_str());runtime->Update(.1f);
        Check(counts()[8]==1&&counts()[9]==1&&removeObject->GetComponentCount()==1&&counts()[2]==releaseBeforeRemove+1&&counts()[3]==destroyBeforeRemove+1,"Typed self-removal was not deferred or released the native instance twice");
        runtime->SetScriptExecutionEnabled(false,error);
        const auto id=script->GetScriptInstanceID();
        Check(runtime->RemoveScriptComponent(object,id,error),error.c_str());
        auto* restored=runtime->AddScriptComponent(object,api->Types[0].Info.TypeID,source,id,error);
        Check(restored&&object->SetComponentIndex(restored,1)&&object->GetComponentAt(1)==restored,"Undo slot restoration failed");
        Check(runtime->AddBuiltinComponent(object,"GameSproutRuntime.Mesh",error),"Builtin catalog creation failed");
        Check(!runtime->AddBuiltinComponent(object,"GameSproutRuntime.Mesh",error),"String catalog creation accepted duplicate builtin");
        Check(object->AddComponent<GameSproutRuntime::IMeshComponent>()==object->GetComponent<GameSproutRuntime::IMeshComponent>(),"Typed builtin AddComponent did not return existing component");
        Check(!runtime->RemoveBuiltinComponent(object,"GameSproutRuntime.Transform",error),"Intrinsic Transform removed");
        auto* cascadeObject=scene->CreateGameObject("Cascading removal");
        Check(runtime->SetScriptExecutionEnabled(true,error),error.c_str());
        Check(cascade(cascadeObject),"Cascading OnCreate/OnRelease returned a destroyed native pointer or released twice");
        Check(runtime->SetScriptExecutionEnabled(false,error),error.c_str());
        for(std::size_t i=0;i<api->Count;++i)Check(runtime->UnregisterScriptType(api->Types[i].Info.TypeID,error),error.c_str());
#if defined(_WIN32)
        FreeLibrary(module);
#else
        dlclose(module);
#endif
        runtime->Update(1); // No retained callable may reference the unloaded library.
        delete runtime;runtime=nullptr;
        std::cout<<"Native library, authoring, lifecycle, fields, typed singleton Add/Get, distinct types, explicit FixedUpdate and safe unload passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';delete runtime;return 1;}
}

#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error) {if(!value)throw std::runtime_error(error);}
int Constructed=0,Created=0,Initialized=0,Released=0,Destroyed=0,Updated=0,Fixed=0;
bool FailCreate=false;
GameSproutRuntime::IGameObject* Peer=nullptr;
class SessionScript final : public GameSproutRuntime::ScriptBehaviour {
public:
    SessionScript(){++Constructed;}
    ~SessionScript() override {++Destroyed;}
    float Value=1;
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(Constructed==2,"OnCreate ran before all native instances were constructed");
        auto* peer=Peer->GetComponent<SessionScript>();
        Check(peer&&peer->Value==9,"OnCreate could not see the restored peer field");
        ++Created;
        if(FailCreate)throw std::runtime_error("Rejected startup fixture");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(Created==2,"OnInitialized ran before all OnCreate callbacks completed");++Initialized;
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {++Updated;}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {++Fixed;}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Released;}
};
void Reset(){Constructed=Created=Initialized=Released=Destroyed=Updated=Fixed=0;}
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* runtime=Export(argv[1]);
    try{
        Check(runtime,"Cannot load Runtime");
        GameSproutRuntime::Internal::RegisterScriptClass<SessionScript>("execution-session-fixture","SessionScript",__FILE__,{"fixture",1},
            {GameSproutRuntime::Internal::MakeField<SessionScript>("value","Value",&SessionScript::Value,1.f)});
        const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().front();
        std::string error;Check(runtime->RegisterScriptType(registration,error),error);
        auto* scene=runtime->CreateScene("Session");
        for(int index=0;index<2;++index){
            auto* object=scene->CreateGameObject("Script");Peer=object;
            auto* script=runtime->AddScriptComponent(object,registration.Info.TypeID,registration.Source,0,error);
            Check(script&&script->SetField("value",GameSproutRuntime::Internal::ScriptValueOf(9.f),error),error);
        }
        runtime->Update(1);Check(Constructed==0,"Edit evaluation constructed game scripts");
        Check(runtime->BeginExecution(error),error);
        Check(Constructed==2&&Created==2&&Initialized==2&&Updated==0&&Fixed==0,"Begin did not finish startup phases before the first tick");
        Check(!runtime->BeginExecution(error),"Reentrant gameplay begin accepted");
        runtime->ProcessPendingEvents();runtime->ProcessPendingEvents();
        Check(Updated==0&&Fixed==0&&Destroyed==0,"Paused event processing evaluated or destroyed components");
        runtime->FixedUpdate(1.f/60);runtime->Update(1.f/60);
        Check(Updated==2&&Fixed==2,"Fixed and variable script dispatch failed");
        Check(runtime->EndExecution(error),error);
        Check(!runtime->IsScriptExecutionEnabled()&&Released==2&&Destroyed==2,"Stop did not release the complete native session");
        Check(runtime->GetScriptTypes().size()==1,"Stop discarded the code catalog");
        Reset();FailCreate=true;
        Check(!runtime->BeginExecution(error)&&error=="Rejected startup fixture","Startup error did not abort the gameplay session");
        Check(!runtime->IsScriptExecutionEnabled()&&Constructed==2&&Released==2&&Destroyed==2&&Initialized==0,"Failed startup leaked instances or ran initialization");
        Reset();FailCreate=false;Check(runtime->BeginExecution(error),error);Check(runtime->EndExecution(error),error);
        Check(Released==2&&Destroyed==2,"Retry after failed start did not recover");
        delete runtime;runtime=nullptr;
        std::cout<<"PASS staged native startup, restored peer fields, event-only pause, fixed dispatch, release and failed-begin retry\n";return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';delete runtime;return 1;}
}

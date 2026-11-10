#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& error) {if(!value) throw std::runtime_error(error);}
int Created=0,Initialized=0,Destroyed=0;
GameSproutRuntime::IGameObject* Expected=nullptr;
}

class ReferenceScript final : public GameSproutRuntime::ScriptBehaviour {
private:
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IGameObject* _Target=nullptr;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IGameObject* _Other=nullptr;
public:
    explicit ReferenceScript(GameSproutRuntime::IGameSproutRuntime*) {}
    GameSproutRuntime::IGameObject* Target() const {return _Target;}
    GameSproutRuntime::IGameObject* Other() const {return _Other;}
    void AssignOther(GameSproutRuntime::IGameObject* value) {_Other=value;}
    void AssignTarget(GameSproutRuntime::IGameObject* value) {_Target=value;}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(_Target==Expected,"OnCreate observed an unresolved/wrong-world reference");++Created;
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(_Target==Expected,"OnInitialized observed an unresolved reference");++Initialized;
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++Destroyed;}
};

namespace GameSproutRuntime::Internal {
template<> struct ScriptFieldAccess<ReferenceScript> {
    static std::vector<ScriptFieldRegistration> Fields() {
        return {
            MakeField<ReferenceScript>("ReferenceScript::_Target","Target",&ReferenceScript::_Target,static_cast<IGameObject*>(nullptr)),
            MakeField<ReferenceScript>("ReferenceScript::_Other","Other",&ReferenceScript::_Other,static_cast<IGameObject*>(nullptr))
        };
    }
};
}

int main(int argc,char** argv) {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        Check(argc==2,"Runtime directory argument missing");
        runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        GameSproutRuntime::Internal::RegisterScriptClass<ReferenceScript>("reference-type","ReferenceScript","Reference.hpp",
            {"0123456789abcdef0123456789abcdef",1},GameSproutRuntime::Internal::ScriptFieldAccess<ReferenceScript>::Fields());
        const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().back();
        std::string error;
        Check(runtime->RegisterScriptType(registration,error),error);
        auto* scene=runtime->CreateScene("First world");
        auto* owner=scene->CreateGameObject("Owner");
        auto* target=scene->CreateGameObject("Target");
        Check(scene->BindObjectIdentity(scene,0,error)&&scene->BindObjectIdentity(owner,1,error)&&scene->BindObjectIdentity(target,2,error),error);
        std::uint64_t fileID=99;
        Check(runtime->GetSceneObjectFileID(target,fileID)&&fileID==2,"Identity lookup lost file ID");
        Check(!scene->BindObjectIdentity(owner,2,error),"Duplicate FileID was accepted");
        Check(!scene->BindObjectIdentity(owner,0,error),"Ordinary node used root identity");
        auto* rejected=scene->CreateGameObject("Rejected batch identity");
        Check(!scene->BindObjectIdentities({{rejected->GetInstanceID(),9},{owner->GetInstanceID(),2}},error),"Conflicting batch was accepted");
        Check(!runtime->GetSceneObjectFileID(rejected,fileID),"Rejected batch partially published an identity");
        auto* script=runtime->AddScriptComponent(owner,registration.Info.TypeID,registration.Source,0,error);
        Check(script,"Cannot attach native script");
        GameSproutRuntime::ScriptValue reference;reference.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
        reference.SceneObject={true,2};
        Check(script->SetField("ReferenceScript::_Target",reference,error),error);
        Check(Created==0&&Initialized==0,"Editing executed user code");
        Expected=target;
        Check(runtime->BeginExecution(error),error);
        auto* native=owner->GetComponent<ReferenceScript>();
        Check(native&&native->Target()==target&&Created==1&&Initialized==1,"Reference not ready at lifecycle barrier");

        native->AssignOther(target); // Direct assignment still belongs to a registered reference slot.
        Check(scene->DestroyGameObject(target),"Destroy referenced target");
        Check(!native->Target()&&!native->Other(),"Destroy left a native dangling reference");
        Check(script->GetFields().at("ReferenceScript::_Target").SceneObject==reference.SceneObject,"Delete lost authored identity needed for undo");
        Check(!runtime->QuerySceneObjectByFileID(scene,2),"Deleted identity resolves to freed storage");
        auto* unrelated=scene->CreateGameObject("Target");
        Check(scene->BindObjectIdentity(unrelated,3,error),error);
        Check(!native->Target(),"Same name or reused storage rebound the reference");
        auto* restored=scene->CreateGameObject("Undo restored Target");
        Check(scene->BindObjectIdentity(restored,2,error),error);
        Check(native->Target()==restored&&!native->Other(),"Undo did not rebind the authored target correctly");
        Check(restored->SetParent(nullptr,false),"Cannot detach target");
        Check(!native->Target(),"Detached target still resolves as a scene reference");
        Check(scene->AddChild(restored,false),"Cannot reattach target in same frame");
        Check(native->Target()==restored&&Initialized==1,"Reattach lost/reinitialized the referring script");

        auto* otherScene=runtime->CreateScene("Second world");
        auto* otherTarget=otherScene->CreateGameObject("Other target with the same FileID");
        Check(otherScene->BindObjectIdentity(otherScene,0,error)&&otherScene->BindObjectIdentity(otherTarget,2,error),error);
        Check(runtime->QuerySceneObjectByFileID(otherScene,2)==otherTarget&&native->Target()==restored,"Scene-local scopes were mixed");
        native->AssignTarget(unrelated);
        auto* additional=scene->CreateGameObject("Additional");
        Check(scene->BindObjectIdentity(additional,4,error),error);
        Check(native->Target()==unrelated,"Identity registration overwrote a gameplay reassignment");
        Check(scene->DestroyGameObject(unrelated)&&!native->Target(),"Direct reassignment was not invalidated on destruction");

        reference.SceneObject={true,0};
        Check(script->SetField("ReferenceScript::_Target",reference,error)&&native->Target()==scene,"Scene root is confused with None");
        reference.SceneObject={false,0};
        Check(script->SetField("ReferenceScript::_Target",reference,error)&&!native->Target(),"Explicit clear failed");
        reference.SceneObject={false,2};
        Check(!script->SetField("ReferenceScript::_Target",reference,error),"Malformed None with a nonzero target accepted");
        Check(runtime->EndExecution(error),error);
        Check(runtime->DestroyScene(scene)&&runtime->DestroyScene(otherScene),"Scene cleanup failed");

        auto* freshScene=runtime->CreateScene("Reloaded world");
        auto* freshOwner=freshScene->CreateGameObject("Owner");
        auto* freshTarget=freshScene->CreateGameObject("Target");
        Check(freshScene->BindObjectIdentity(freshScene,0,error)&&freshScene->BindObjectIdentity(freshOwner,1,error)&&freshScene->BindObjectIdentity(freshTarget,2,error),error);
        script=runtime->AddScriptComponent(freshOwner,registration.Info.TypeID,registration.Source,0,error);
        reference.SceneObject={true,2};Check(script&&script->SetField("ReferenceScript::_Target",reference,error),error);
        Expected=freshTarget;Check(runtime->BeginExecution(error),error);
        Check(freshOwner->GetComponent<ReferenceScript>()->Target()==freshTarget,"Reload used previous world's address or InstanceID");
        Check(Created==2&&Initialized==2,"Unexpected script initialization count");
        Check(runtime->EndExecution(error),error);
        delete runtime;runtime=nullptr;
        Check(Destroyed==2,"Script instances were leaked or released twice");
        std::cout<<"PASS authored references, lifecycle, invalidation, undo/rebind and world isolation\n";
        return 0;
    } catch(const std::exception& exception) {
        std::cerr<<"FAIL "<<exception.what()<<'\n';delete runtime;return 1;
    }
}

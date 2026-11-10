#pragma once
#include <limits>

namespace {
GameSproutRuntime::IGameObject* EvolutionTarget=nullptr;
class FieldEvolutionScript final:public GameSproutRuntime::ScriptBehaviour {
public:
    explicit FieldEvolutionScript(GameSproutRuntime::IGameSproutRuntime*) {}
    double Kept=-1,Added=-1;
    bool Changed=false;
    GameSproutRuntime::IGameObject* Target=nullptr;
    GameSproutRuntime::IGameObject* Empty=nullptr;
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        Check(Kept==7.5&&Added==9.25&&Changed&&Target==EvolutionTarget&&!Empty,
            "Persisted schema defaults/references were not restored before OnCreate");
    }
};
class RestoreCallbackCheck final:public GameSproutRuntime::IRuntimeListener {
public:
    GameSproutRuntime::IScriptComponent* Component=nullptr;
    bool Rejected=false;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {
        if(!Component)return;
        std::string error;Rejected=!Component->RestoreSerializedFields(Component->GetFields(),error);
    }
};
void CheckSerializedFieldEvolution(GameSproutRuntime::IGameSproutRuntime& runtime) {
    const GameSproutRuntime::ResourceReference source{"abcdef0123456789abcdef0123456789",1};
    GameSproutRuntime::Internal::RegisterScriptClass<FieldEvolutionScript>("field-evolution","FieldEvolutionScript","Evolution.hpp",source,{
        GameSproutRuntime::Internal::MakeField<FieldEvolutionScript>("keep","Keep",&FieldEvolutionScript::Kept,2.0),
        GameSproutRuntime::Internal::MakeField<FieldEvolutionScript>("added","Added",&FieldEvolutionScript::Added,9.25),
        GameSproutRuntime::Internal::MakeField<FieldEvolutionScript>("changed","Changed",&FieldEvolutionScript::Changed,true),
        GameSproutRuntime::Internal::MakeField<FieldEvolutionScript>("target","Target",&FieldEvolutionScript::Target,static_cast<GameSproutRuntime::IGameObject*>(nullptr)),
        GameSproutRuntime::Internal::MakeField<FieldEvolutionScript>("empty","Empty",&FieldEvolutionScript::Empty,static_cast<GameSproutRuntime::IGameObject*>(nullptr))
    });
    const auto registration=GameSproutRuntime::Internal::ScriptModuleTypes().back();
    auto* scene=runtime.CreateScene("Field evolution");auto* owner=scene->CreateGameObject("Owner");auto* target=scene->CreateGameObject("Internal reference");
    std::string error;
    Check(scene->BindObjectIdentity(scene,0,error)&&scene->BindObjectIdentity(owner,1,error)&&scene->BindObjectIdentity(target,2,error),error.c_str());
    auto* component=runtime.AddScriptComponent(owner,registration.Info.TypeID,source,0,error);Check(component,error.c_str());
    std::map<std::string,GameSproutRuntime::ScriptValue> stored;
    stored["keep"]=GameSproutRuntime::Internal::ScriptValueOf(7.5);
    stored["removed"]=GameSproutRuntime::Internal::ScriptValueOf(false);
    stored["changed"]=GameSproutRuntime::Internal::ScriptValueOf(std::string("old string kind"));
    stored["target"].Kind=GameSproutRuntime::ScriptValueKind::SceneObject;stored["target"].SceneObject={true,2};
    stored["empty"].Kind=GameSproutRuntime::ScriptValueKind::SceneObject;
    Check(component->RestoreSerializedFields(stored,error)&&component->GetFields()==stored,"Missing script type discarded serialized values before registration");
    Check(runtime.RegisterScriptType(registration,error),error.c_str());
    const auto aligned=component->GetFields();
    Check(aligned.size()==5&&!aligned.count("removed")&&aligned.at("keep").Number==7.5&&aligned.at("added").Number==9.25&&
          aligned.at("changed").Kind==GameSproutRuntime::ScriptValueKind::Boolean&&aligned.at("changed").Boolean&&
          aligned.at("target").SceneObject==stored.at("target").SceneObject&&!aligned.at("empty").SceneObject.Assigned,
          "Registering a missing type did not project stored values to its declared schema");
    Check(component->RestoreSerializedFields(stored,error)&&component->GetFields()==aligned,"Registered restore differs from later type registration projection");
    Check(!component->SetField("removed",stored.at("removed"),error)&&!component->SetField("changed",stored.at("changed"),error)&&
          !component->ReplaceFields(stored,error)&&component->GetFields()==aligned,"Strict field commands accepted removed or mismatched fields");
    const auto rejected=[&](const std::map<std::string,GameSproutRuntime::ScriptValue>& values) {
        Check(!component->RestoreSerializedFields(values,error)&&component->GetFields()==aligned,"Malformed serialized fields were accepted or partially published");
    };
    auto malformed=stored;malformed["removed"].Kind=GameSproutRuntime::ScriptValueKind::Number;malformed["removed"].Number=std::numeric_limits<double>::quiet_NaN();rejected(malformed);
    malformed=stored;malformed["removed"].Kind=GameSproutRuntime::ScriptValueKind::Vector3;malformed["removed"].Vector.X=std::numeric_limits<float>::infinity();rejected(malformed);
    malformed=stored;malformed["target"].SceneObject={false,2};rejected(malformed);
    malformed=stored;malformed["removed"].Kind=GameSproutRuntime::ScriptValueKind::Resource;malformed["removed"].Resource={"invalid",1};rejected(malformed);
    malformed=stored;malformed[std::string("bad\0id",6)]=stored.at("keep");rejected(malformed);
    RestoreCallbackCheck callback;callback.Component=component;Check(runtime.RegisterListener(&callback),"Cannot subscribe restore callback check");
    Check(component->RestoreSerializedFields(stored,error),error.c_str());runtime.ProcessPendingEvents();
    Check(callback.Rejected,"Serialized restore reentered Runtime event dispatch");runtime.UnregisterListener(&callback);
    EvolutionTarget=target;Check(runtime.BeginExecution(error),error.c_str());
    auto* native=owner->GetComponent<FieldEvolutionScript>();Check(native&&native->Target==target&&native->Added==9.25,"New field defaults or internal reference not present on native object");
    Check(!component->RestoreSerializedFields(stored,error)&&component->GetFields()==aligned,"Serialized restore replaced a running native instance");
    Check(runtime.EndExecution(error),error.c_str());
    auto rootReference=stored;rootReference["target"].SceneObject={true,0};Check(component->RestoreSerializedFields(rootReference,error),error.c_str());
    EvolutionTarget=scene;Check(runtime.BeginExecution(error),error.c_str());Check(owner->GetComponent<FieldEvolutionScript>()->Target==scene,"Scene root file ID zero was confused with a null reference");
    Check(runtime.EndExecution(error),error.c_str());
    Check(runtime.DestroyScene(scene)&&runtime.UnregisterScriptType(registration.Info.TypeID,error),"Cannot clean up serialized field fixture");EvolutionTarget=nullptr;
}
}

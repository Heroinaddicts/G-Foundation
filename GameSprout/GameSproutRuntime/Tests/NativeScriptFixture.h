#pragma once
#include "RuntimeScriptModule.h"
#include <stdexcept>

extern int NativeScriptCounts[13];
class NativeObserver final : public GameSproutRuntime::ScriptBehaviour {
public:
    explicit NativeObserver(GameSproutRuntime::IGameSproutRuntime*) {}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override;
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {}
    void Update(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {}
};
GAMESPROUT_SCRIPT(NativeObserver)

// The authored class has no constructor forwarding or registration metadata.
class NativeMover final : public GameSproutRuntime::ScriptBehaviour {
public:
    explicit NativeMover(GameSproutRuntime::IGameSproutRuntime*) {}
    ~NativeMover() override {++NativeScriptCounts[3];}
    float Speed=1;
    bool Throw=false;
    bool DisableOnCreate=false;
    bool AddObserver=false;
    bool RemoveSelf=false;
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {
        ++NativeScriptCounts[0];
        if(_GameObject->AddComponent<NativeMover>()==nullptr)++NativeScriptCounts[4];
        if(_GameObject->GetComponent<NativeMover>()==this)++NativeScriptCounts[5];
        if(AddObserver && NativeScriptCounts[0]==1) {
            auto* observer=_GameObject->AddComponent<NativeObserver>();
            if(!observer || _GameObject->GetComponent<NativeMover>()!=this || _GameObject->GetComponent<NativeObserver>()!=observer)
                throw std::runtime_error("Nested script creation broke typed component lookup");
        }
        if(!DisableOnCreate) return;
        const auto destroyed=NativeScriptCounts[3];
        for(std::size_t i=0;i<_GameObject->GetComponentCount();++i) {
            auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(_GameObject->GetComponentAt(i));
            if(script&&script->GetFields().contains("DisableOnCreate")&&script->GetFields().at("DisableOnCreate").Boolean)script->SetEnabled(false);
        }
        if(NativeScriptCounts[3]!=destroyed)throw std::runtime_error("Self-disable destroyed this inside OnCreate");
        Speed=123;++NativeScriptCounts[10];
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {
        ++NativeScriptCounts[1];
        auto position=_GameObject->GetTransform()->GetLocalPosition();position.Y=Speed;
        _GameObject->GetTransform()->SetLocalPosition(position);
    }
    void Update(GameSproutRuntime::IGameSproutRuntime*,float delta) override {
        if(RemoveSelf) {
            const auto destroyed=NativeScriptCounts[3];
            if(_GameObject->RemoveComponent<NativeMover>())++NativeScriptCounts[8];
            if(NativeScriptCounts[3]!=destroyed)throw std::runtime_error("RemoveComponent destroyed this inside Update");
            ++NativeScriptCounts[9];return;
        }
        if(Throw) throw std::runtime_error("Fixture script error");
        auto position=_GameObject->GetTransform()->GetLocalPosition();position.X+=Speed*delta;
        _GameObject->GetTransform()->SetLocalPosition(position);
    }
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float delta) override {
        ++NativeScriptCounts[7];
        auto position=_GameObject->GetTransform()->GetLocalPosition();position.Z+=Speed*delta;
        _GameObject->GetTransform()->SetLocalPosition(position);
    }
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++NativeScriptCounts[2];}
};
GAMESPROUT_SCRIPT(NativeMover)
inline void NativeObserver::OnCreate(GameSproutRuntime::IGameSproutRuntime*) {
    if(!NativeScriptCounts[6]&&!_GameObject->GetComponent<NativeMover>())throw std::runtime_error("Nested observer cannot find creating mover");
    ++NativeScriptCounts[6];
}

class CascadeB final : public GameSproutRuntime::ScriptBehaviour {
public:
    explicit CascadeB(GameSproutRuntime::IGameSproutRuntime*) {}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override;
};
class CascadeA final : public GameSproutRuntime::ScriptBehaviour {
public:
    explicit CascadeA(GameSproutRuntime::IGameSproutRuntime*) {}
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {_GameObject->RemoveComponent<CascadeB>();}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {++NativeScriptCounts[11];}
};
inline void CascadeB::OnRelease(GameSproutRuntime::IGameSproutRuntime*) {
    ++NativeScriptCounts[12];_GameObject->RemoveComponent<CascadeA>();
}
GAMESPROUT_SCRIPT(CascadeA)
GAMESPROUT_SCRIPT(CascadeB)

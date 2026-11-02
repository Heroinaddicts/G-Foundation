#include "NativeScriptFixture.h"
#include "Internal/RuntimeScriptBindings.h"

int NativeScriptCounts[13]{};
// This is test-only generated glue. Real Assets scripts get this from
// ScriptCompiler; NativeScriptFixture.h contains only ordinary C++ classes.
static const bool MoverRegistered=GameSproutRuntime::Internal::RegisterScriptClass<NativeMover>(
    "30bf902c414d42baa069c36d4277f82e","NativeMover",__FILE__,{"30bf902c414d42baa069c36d4277f82e",1},{
        GameSproutRuntime::Internal::MakeField<NativeMover>("Speed","Speed",&NativeMover::Speed,1.0f),
        GameSproutRuntime::Internal::MakeField<NativeMover>("Throw","Throw",&NativeMover::Throw,false),
        GameSproutRuntime::Internal::MakeField<NativeMover>("DisableOnCreate","DisableOnCreate",&NativeMover::DisableOnCreate,false),
        GameSproutRuntime::Internal::MakeField<NativeMover>("AddObserver","AddObserver",&NativeMover::AddObserver,false),
        GameSproutRuntime::Internal::MakeField<NativeMover>("RemoveSelf","RemoveSelf",&NativeMover::RemoveSelf,false)});
static const bool ObserverRegistered=GameSproutRuntime::Internal::RegisterScriptClass<NativeObserver>(
    "40bf902c414d42baa069c36d4277f82e","NativeObserver",__FILE__,{"40bf902c414d42baa069c36d4277f82e",1});
static const bool CascadeARegistered=GameSproutRuntime::Internal::RegisterScriptClass<CascadeA>(
    "50bf902c414d42baa069c36d4277f82e","CascadeA",__FILE__,{"50bf902c414d42baa069c36d4277f82e",1});
static const bool CascadeBRegistered=GameSproutRuntime::Internal::RegisterScriptClass<CascadeB>(
    "60bf902c414d42baa069c36d4277f82e","CascadeB",__FILE__,{"60bf902c414d42baa069c36d4277f82e",1});
GAMESPROUT_GAME_MODULE()
extern "C" GAMESPROUT_SCRIPT_EXPORT const int* GetNativeScriptCounts(){return NativeScriptCounts;}
extern "C" GAMESPROUT_SCRIPT_EXPORT bool CheckNativeCascadeRemoval(GameSproutRuntime::IGameObject* object) {
    if(!object->AddComponent<CascadeB>())return false;
    const auto releasedA=NativeScriptCounts[11],releasedB=NativeScriptCounts[12];
    auto* result=object->AddComponent<CascadeA>();
    return result==nullptr&&object->GetComponentCount()==1&&
        NativeScriptCounts[11]==releasedA+1&&NativeScriptCounts[12]==releasedB+1;
}

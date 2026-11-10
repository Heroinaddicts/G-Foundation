#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/ScriptComponent.h"
#include <exception>

namespace GameSproutRuntime {
bool Runtime::BeginExecution(std::string& error) {
    error.clear();
    if(IsExecuting()||_ShuttingDown||_ScriptExecutionEnabled||_ConstructionDepth||!_ActiveGameModules.empty()) {
        error="Runtime is busy or gameplay execution is already active";return false;
    }
    bool success=false;
    try {
        success=StartGameModules(error)&&SetScriptExecutionState(true,error);
        if(success) {
            FlushCommands();
            success=InitializePendingComponents(error,true);
        }
        if(success)success=PreparePhysics(error);
        if(success && _ConstructionDepth) {
            error="A script left an object construction scope open during startup";
            success=false;
        }
    } catch(const std::exception& exception) {error=exception.what();success=false;}
      catch(...) {error="Cannot initialize gameplay code";success=false;}
    if(!success) {
        // BeginExecution started outside construction. Scopes opened by failed
        // startup callbacks cannot keep the entire host permanently suspended.
        _ConstructionDepth=0;
        std::string cleanup;
        try {SetScriptExecutionState(false,cleanup);} catch(...) {}
        StopGameModules();
        ResetPhysics();
        _ConstructionDepth=0;
        if(error.empty()) error="Gameplay initialization failed";
        return false;
    }
    SetInputEnabled(true);
    return true;
}
bool Runtime::EndExecution(std::string& error) {
    if(IsExecuting()||_ShuttingDown){error="Cannot end gameplay during a Runtime callback";return false;}
    SetInputEnabled(false);
    const bool result=SetScriptExecutionState(false,error);
    if(result) {
        StopGameModules();
        ResetPhysics();
    }
    return result;
}
void Runtime::ProcessPendingEvents() {
    if(IsExecuting()||_ShuttingDown) return;
    PauseShaderClock();
    _Updating=true;
    try {
        // Structural commands normally flush at the operation boundary. This
        // entry never invokes component Update/FixedUpdate, including Animator.
        {RuntimeOperation operation(*this);}
        DispatchAnimationEvents();
        DispatchEvents();
        RefreshObservedScriptProperties();
        DispatchObjectPropertyChanges();
    } catch(...) {_Updating=false;throw;}
    _Updating=false;
}
}

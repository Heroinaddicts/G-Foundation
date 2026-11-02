#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/ScriptComponent.h"
#include <exception>

namespace GameSproutRuntime {
bool Runtime::BeginExecution(std::string& error) {
    error.clear();
    if(IsExecuting()||_ShuttingDown||_ScriptExecutionEnabled||_ConstructionDepth) {
        error="Runtime is busy or gameplay execution is already active";return false;
    }
    if(!SetScriptExecutionEnabled(true,error)) return false;
    bool success=false;
    try {
        FlushCommands();
        success=InitializePendingComponents(error,true);
        if(success && _ConstructionDepth) {
            error="A script left an object construction scope open during startup";
            success=false;
        }
    } catch(const std::exception& exception) {error=exception.what();success=false;}
      catch(...) {error="Cannot initialize gameplay scripts";success=false;}
    if(!success) {
        // BeginExecution started outside construction. Scopes opened by failed
        // startup callbacks cannot keep the entire host permanently suspended.
        _ConstructionDepth=0;
        std::string cleanup;
        SetScriptExecutionEnabled(false,cleanup);
        _ConstructionDepth=0;
        if(error.empty()) error="Native script initialization failed";
        return false;
    }
    SetInputEnabled(true);
    return true;
}
bool Runtime::EndExecution(std::string& error) {
    if(IsExecuting()||_ShuttingDown){error="Cannot end gameplay during a Runtime callback";return false;}
    SetInputEnabled(false);
    return SetScriptExecutionEnabled(false,error);
}
void Runtime::ProcessPendingEvents() {
    if(IsExecuting()||_ShuttingDown) return;
    _Updating=true;
    try {
        // Structural commands normally flush at the operation boundary. This
        // entry never invokes component Update/FixedUpdate, including Animator.
        {RuntimeOperation operation(*this);}
        DispatchAnimationEvents();
        DispatchEvents();
    } catch(...) {_Updating=false;throw;}
    _Updating=false;
}
}

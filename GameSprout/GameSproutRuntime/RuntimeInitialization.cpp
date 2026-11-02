#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Components/ScriptComponent.h"
#include <algorithm>
#include <exception>
#include <limits>

namespace GameSproutRuntime {
bool Runtime::BeginConstruction(std::string& error) {
    error.clear();
    if(_ShuttingDown || _ConstructionDepth==std::numeric_limits<std::size_t>::max()) {
        error="Runtime cannot begin object construction";return false;
    }
    ++_ConstructionDepth;
    return true;
}
bool Runtime::EndConstruction(std::string& error) {
    error.clear();
    if(!_ConstructionDepth) {error="Runtime has no open construction scope";return false;}
    --_ConstructionDepth;
    // A scope opened by a component commits at the enclosing callback/traversal
    // boundary, after pending Attach/Remove/Destroy commands have been applied.
    if(_ConstructionDepth || _Depth || _Flushing || _InitializingComponents) return true;
    FlushCommands();
    return InitializePendingComponents(error,true);
}
void Runtime::QueueComponentInitialization(GameObjectState& node,std::uint64_t serial) {
    const auto entry=std::find_if(node.Components.begin(),node.Components.end(),
        [serial](const auto& component){return component.Serial==serial;});
    if(entry==node.Components.end() || entry->PendingRemove || entry->Initialized ||
       entry->InitializationFailed || entry->InitializationQueued) return;
    _PendingInitializations.push_back({node.ID,serial});
    entry->InitializationQueued=true;
}
void Runtime::QueueObjectInitialization(GameObjectState& node,bool recursive) {
    for(const auto& component:node.Components) QueueComponentInitialization(node,component.Serial);
    if(recursive) for(auto* child:node.Children) QueueObjectInitialization(*child,true);
}
void Runtime::QueueScriptInitialization(ScriptComponent& script) {
    auto* node=Find(script.GetGameObject());
    if(!IsUsable(node) || script.IsInitialized()) return;
    const auto entry=std::find_if(node->Components.begin(),node->Components.end(),
        [&script](const auto& component){return component.Pointer==&script;});
    if(entry==node->Components.end() || entry->PendingRemove) return;
    entry->Initialized=false;
    entry->InitializationFailed=false;
    QueueComponentInitialization(*node,entry->Serial);
}
void Runtime::NotifyInitializationError(GameObjectState& node,IComponent& component,const std::string& error) {
    if(!IsInScene(&node)) return;
    SceneNodeEvent event;
    event.Type=SceneNodeEventType::OperationFailed;
    event.SceneID=node.GetScene()->GetInstanceID();
    event.ObjectID=node.ID;
    event.ComponentType=component.GetTypeName();
    event.Error=error;
    _Events.push_back({++_EventSequence,std::move(event)});
}
bool Runtime::InitializePendingComponents(std::string& error,bool allowScripts) {
    error.clear();
    if(_ConstructionDepth || _Depth || _Flushing || _InitializingComponents || _ShuttingDown) return true;
    struct InitializationScope {
        Runtime& Owner;
        explicit InitializationScope(Runtime& owner):Owner(owner) {Owner._InitializingComponents=true;}
        ~InitializationScope() {Owner._StartingScripts=false;Owner._InitializingComponents=false;}
    } initializing(*this);
    // Only identities survive a callback: it may remove components, destroy a
    // whole scene or allocate new objects at the same addresses.
    const auto resolve=[this](const InitializationRecord& record)->GameObjectState::ComponentEntry* {
        auto* node=Find(record.ObjectID);
        if(!IsUsable(node)) return nullptr;
        const auto entry=std::find_if(node->Components.begin(),node->Components.end(),
            [&record](const auto& component){return component.Serial==record.Serial;});
        return entry!=node->Components.end() && !entry->PendingRemove?&*entry:nullptr;
    };
    const auto eligible=[this](const GameObjectState::ComponentEntry& entry)->bool {
        const auto* node=Find(entry.Pointer->GetGameObject());
        if(!IsInScene(node) || node->PendingAttach) return false;
        const auto* script=dynamic_cast<ScriptComponent*>(entry.Pointer);
        return !script || (_ScriptExecutionEnabled && script->IsEnabled() &&
            !script->HasFailed() &&
            script->GetGameObject()->IsActiveInHierarchy());
    };
    const auto fail=[&](const InitializationRecord& record,const std::string& message) {
        if(error.empty()) error=message;
        if(auto* entry=resolve(record)) {
            entry->InitializationQueued=false;
            entry->InitializationFailed=true;
            if(auto* script=dynamic_cast<ScriptComponent*>(entry->Pointer)) {
                if(!script->HasFailed()) script->ReportInitializationFailure(message);
            } else NotifyInitializationError(*Find(record.ObjectID),*entry->Pointer,message);
        }
    };
    std::deque<InitializationRecord> deferredScripts;
    while(!_PendingInitializations.empty()) {
        std::vector<InitializationRecord> ready;
        _StartingScripts=true;
        // Exhaust OnCreate additions before starting this cohort's Initialized
        // callbacks. Each invocation has its own structure barrier.
        while(!_PendingInitializations.empty() && !_ConstructionDepth) {
            std::deque<InitializationRecord> batch;
            batch.swap(_PendingInitializations);
            std::vector<InitializationRecord> prepared;
            for(const auto& record:batch) {
                auto* entry=resolve(record);
                if(!entry) continue;
                if(entry->Initialized || entry->InitializationFailed) {entry->InitializationQueued=false;continue;}
                // Detached objects remain configurable until the host's frame
                // end. Do not reserve their queue entry: attaching their whole
                // subtree later must be able to enqueue it again.
                const auto* node=Find(record.ObjectID);
                if(!IsInScene(node) || node->PendingAttach) {entry->InitializationQueued=false;continue;}
                auto* script=dynamic_cast<ScriptComponent*>(entry->Pointer);
                if(script && !allowScripts && _ScriptExecutionEnabled) {
                    deferredScripts.push_back(record);continue;
                }
                if(!eligible(*entry)) {entry->InitializationQueued=false;continue;}
                if(script) {
                    bool available=script->IsScriptTypeAvailable();
                    bool success=false;
                    std::string message;
                    {
                        RuntimeOperation operation(*this);
                        success=available && script->PrepareInstance();
                        if(!success) message=script->GetScriptError();
                    }
                    if(!success) {
                        // A constructor can deliberately disable/remove itself.
                        entry=resolve(record);
                        if(!entry || !eligible(*entry)) {
                            if(entry) entry->InitializationQueued=false;
                            if(!message.empty()) fail(record,message);
                            continue;
                        }
                        fail(record,message.empty()?"Cannot construct native script: "+script->GetScriptTypeID():message);
                        continue;
                    }
                }
                prepared.push_back(record);
                ready.push_back(record);
            }
            // All peers in the batch have their native instances and authored
            // fields before any of their OnCreate callbacks can query them.
            for(const auto& record:prepared) {
                if(_ConstructionDepth) break;
                auto* entry=resolve(record);
                if(!entry || !eligible(*entry)) continue;
                auto* script=dynamic_cast<ScriptComponent*>(entry->Pointer);
                if(!script) continue;
                bool success;
                std::string message;
                {
                    RuntimeOperation operation(*this);
                    success=script->InvokeCreate();
                    if(!success) message=script->GetScriptError();
                }
                if(!success) fail(record,message.empty()?"Native script OnCreate failed":message);
                else if(auto* current=resolve(record);current && eligible(*current)) {
                    auto* live=dynamic_cast<ScriptComponent*>(current->Pointer);
                    if(live && !live->HasCreatedInstance()) {
                        current->InitializationQueued=false;
                        QueueScriptInitialization(*live);
                    }
                }
                if(_ConstructionDepth) break;
            }
        }
        _StartingScripts=false;
        if(!error.empty() || _ConstructionDepth) {
            // No Initialized callback runs for a cohort whose creation failed.
            // Successful peers stay pending; a failed BeginExecution will stop
            // every native instance before a later retry rebuilds them.
            for(const auto& record:ready) if(auto* entry=resolve(record)) {
                entry->InitializationQueued=false;
                QueueComponentInitialization(*Find(record.ObjectID),record.Serial);
            }
            break;
        }
        for(const auto& record:ready) {
            if(_ConstructionDepth) break;
            auto* entry=resolve(record);
            if(!entry || entry->Initialized || entry->InitializationFailed) continue;
            entry->InitializationQueued=false;
            if(!eligible(*entry)) continue;
            bool success=true;
            std::string message;
            try {
                RuntimeOperation operation(*this);
                if(auto* script=dynamic_cast<ScriptComponent*>(entry->Pointer)) {
                    success=script->InvokeInitialized();
                    if(!success) message=script->GetScriptError();
                } else entry->Pointer->OnInitialized(this);
            } catch(const std::exception& exception) {success=false;message=exception.what();}
              catch(...) {success=false;message="Component OnInitialized threw an exception";}
            if(!success) fail(record,message.empty()?"Component initialization failed":message);
            else if(auto* current=resolve(record)) {
                auto* script=dynamic_cast<ScriptComponent*>(current->Pointer);
                current->Initialized=eligible(*current) && (!script || script->IsInitialized());
                if(script && !current->Initialized && eligible(*current)) QueueScriptInitialization(*script);
            }
        }
        if(_ConstructionDepth) {
            for(const auto& record:ready) if(auto* entry=resolve(record);entry && !entry->Initialized) {
                entry->InitializationQueued=false;
                QueueComponentInitialization(*Find(record.ObjectID),record.Serial);
            }
            break;
        }
        if(!error.empty()) break;
        // OnInitialized additions are a new cohort. They receive their own
        // creation/commit phase before any new component is allowed to tick.
    }
    for(const auto& record:deferredScripts) _PendingInitializations.push_back(record);
    // Entry was outside construction. A failing user callback must not leave
    // newly opened scopes blocking every later frame and rollback operation.
    if(!error.empty()) _ConstructionDepth=0;
    return error.empty();
}
}

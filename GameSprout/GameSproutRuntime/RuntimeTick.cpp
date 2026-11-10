#include "GameSproutRuntime.h"
#include "GameObject.h"
#include "Scene.h"
#include "Components/AnimatorComponent.h"
#include "Components/ScriptComponent.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace GameSproutRuntime {
std::vector<Runtime::FrameComponent> Runtime::SnapshotFrameComponents(bool fixedOnly) const {
    // Capture only registered work. Static scene nodes never enter a frame walk.
    // Identities, rather than borrowed pointers, survive phase-boundary commits.
    std::set<std::uint64_t> objects;
    for(const auto& [id,counts]:_TickObjects)
        if(fixedOnly?counts.Fixed:(counts.Variable||counts.Fixed||counts.Early||counts.Late))objects.insert(id);
    if(!fixedOnly)for(const auto& [id,count]:_AnimationObjects)objects.insert(id);
    std::vector<FrameComponent> result;
    for(const auto id:objects) {
        const auto* node=Find(id);
        if(!IsInScene(node)||!node->SceneOwner->IsSimulationEnabled()||!node->IsActiveInHierarchy())continue;
        for(const auto& entry:node->Components) {
            if(entry.PendingRemove||!entry.Initialized||entry.InitializationFailed)continue;
            const bool animation=!fixedOnly&&dynamic_cast<AnimatorComponent*>(entry.Pointer);
            const auto mask=fixedOnly?std::uint8_t(entry.TickMask&FixedTick):entry.TickMask;
            if(mask||animation)result.push_back({id,entry.Serial,mask,animation,false});
        }
    }
    return result;
}
IComponent* Runtime::ResolveFrameComponent(const FrameComponent& record) const {
    if(record.Skipped||_ConstructionDepth)return nullptr;
    auto* node=Find(record.ObjectID);
    if(!IsInScene(node)||!node->SceneOwner->IsSimulationEnabled()||!node->IsActiveInHierarchy())return nullptr;
    const auto entry=std::find_if(node->Components.begin(),node->Components.end(),
        [&](const auto& value){return value.Serial==record.Serial;});
    return entry!=node->Components.end()&&!entry->PendingRemove&&entry->Initialized&&!entry->InitializationFailed
        ?entry->Pointer:nullptr;
}
void Runtime::RecoverCallbackConstruction(FrameComponent& record,IComponent& component,std::size_t expectedDepth) {
    if(_ConstructionDepth==expectedDepth)return;
    // A component cannot carry an assembly scope across its callback boundary.
    // Restore before reporting: ScriptComponent may already have caught a user
    // exception internally, and its release callback can open another scope.
    _ConstructionDepth=expectedDepth;record.Skipped=true;
    const std::string message="Component callback left an unbalanced object construction scope";
    try {
        if(auto* script=dynamic_cast<ScriptComponent*>(&component);script&&!script->HasFailed())
            script->ReportInitializationFailure(message);
        else if(auto* node=Find(record.ObjectID))NotifyInitializationError(*node,component,message);
    }catch(...) {}
    _ConstructionDepth=expectedDepth;
}
void Runtime::TickComponentPhase(std::vector<FrameComponent>& batch,std::uint8_t phase,float delta) {
    RuntimeOperation operation(*this);
    for(auto& record:batch) {
        if(!(record.Mask&phase))continue;
        auto* component=ResolveFrameComponent(record);if(!component){record.Skipped=true;continue;}
        const auto constructionDepth=_ConstructionDepth;
        try {
            switch(phase) {
            case EarlyTick:component->EarlyUpdate(this,delta);break;
            case FixedTick:component->FixedUpdate(this,delta);break;
            case UpdateTick:component->Update(this,delta);break;
            case LateTick:component->LateUpdate(this,delta);break;
            }
        }catch(const std::exception& exception) {
            record.Skipped=true;
            if(auto* node=Find(record.ObjectID))NotifyInitializationError(*node,*component,exception.what());
        }catch(...) {
            record.Skipped=true;
            if(auto* node=Find(record.ObjectID))NotifyInitializationError(*node,*component,"Component frame callback threw an exception");
        }
        RecoverCallbackConstruction(record,*component,constructionDepth);
    }
}
void Runtime::TickAnimationPhase(std::vector<FrameComponent>& batch,float delta) {
    RuntimeOperation operation(*this);
    for(auto& record:batch) {
        if(!record.Animation)continue;
        auto* component=ResolveFrameComponent(record);if(!component){record.Skipped=true;continue;}
        const auto constructionDepth=_ConstructionDepth;
        try {static_cast<AnimatorComponent*>(component)->Evaluate(delta);}
        catch(const std::exception& exception) {
            record.Skipped=true;
            if(auto* node=Find(record.ObjectID))NotifyInitializationError(*node,*component,exception.what());
        }catch(...) {
            record.Skipped=true;
            if(auto* node=Find(record.ObjectID))NotifyInitializationError(*node,*component,"Animator evaluation threw an exception");
        }
        RecoverCallbackConstruction(record,*component,constructionDepth);
    }
}
void Runtime::FinishTickNotifications() {
    DispatchEvents();
    RefreshObservedScriptProperties();
    DispatchObjectPropertyChanges();
}
void Runtime::AdvanceFrame(float deltaSeconds,std::uint32_t fixedStepCount,float fixedDeltaSeconds) {
    if(_ShuttingDown||IsExecuting()||_ConstructionDepth||!std::isfinite(deltaSeconds)||deltaSeconds<0||
       !std::isfinite(fixedDeltaSeconds)||fixedDeltaSeconds<0||(fixedStepCount&&fixedDeltaSeconds<=0))return;
    struct UpdatingScope {bool& Updating;explicit UpdatingScope(bool& updating):Updating(updating){Updating=true;}~UpdatingScope(){Updating=false;}} updating(_Updating);
    AdvanceShaderClock(deltaSeconds);
    std::string error;
    InitializePendingComponents(error,deltaSeconds>0||fixedStepCount>0);
    auto batch=SnapshotFrameComponents(false);
    DispatchUIPointer();
    TickGameModulePhase(EarlyTick,deltaSeconds);
    TickComponentPhase(batch,EarlyTick,deltaSeconds);
    for(std::uint32_t step=0;step<fixedStepCount&&!_ConstructionDepth;++step) {
        TickGameModulePhase(FixedTick,fixedDeltaSeconds);
        TickComponentPhase(batch,FixedTick,fixedDeltaSeconds);
        if(!_ConstructionDepth)StepPhysics(fixedDeltaSeconds);
    }
    TickGameModulePhase(UpdateTick,deltaSeconds);
    TickComponentPhase(batch,UpdateTick,deltaSeconds);
    TickAnimationPhase(batch,deltaSeconds);
    // Root motion and animation notifications finish before follow-camera and
    // other LateUpdate consumers observe this frame's final animated pose.
    DispatchAnimationEvents();
    TickGameModulePhase(LateTick,deltaSeconds);
    TickComponentPhase(batch,LateTick,deltaSeconds);
    // Additions made by any phase may initialize now, but are outside the frozen
    // batch and cannot receive just the latter half of their first frame.
    InitializePendingComponents(error,deltaSeconds>0||fixedStepCount>0);
    // Late callbacks and tail initialization can Play/Seek too. Deliver one
    // further fixed batch before the host renders; never drain recursively.
    DispatchAnimationEvents();
    FinishTickNotifications();
}
void Runtime::Update(float deltaSeconds) {AdvanceFrame(deltaSeconds,0,0);}
void Runtime::FixedUpdate(float fixedDeltaSeconds) {
    // Explicit fixed stepping remains available for simulations/tests. Hosts
    // wanting Early before Physics use AdvanceFrame once for the whole frame.
    if(_ShuttingDown||IsExecuting()||_ConstructionDepth||!std::isfinite(fixedDeltaSeconds)||fixedDeltaSeconds<=0)return;
    struct UpdatingScope {bool& Updating;explicit UpdatingScope(bool& updating):Updating(updating){Updating=true;}~UpdatingScope(){Updating=false;}} updating(_Updating);
    std::string error;
    InitializePendingComponents(error,true);
    auto batch=SnapshotFrameComponents(true);
    TickGameModulePhase(FixedTick,fixedDeltaSeconds);
    TickComponentPhase(batch,FixedTick,fixedDeltaSeconds);
    if(!_ConstructionDepth)StepPhysics(fixedDeltaSeconds);
    DispatchAnimationEvents();
    InitializePendingComponents(error,true);
    FinishTickNotifications();
}
}

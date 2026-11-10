#include "GameSproutRuntime.h"
#include <algorithm>
#include <exception>
#include <utility>

namespace GameSproutRuntime {
IModule* Runtime::FindModule(const std::string& name) {
    const auto found=std::lower_bound(_ActiveGameModules.begin(),_ActiveGameModules.end(),name,
        [](const auto& module,const auto& key) {return module.Registration->Name<key;});
    return found!=_ActiveGameModules.end()&&found->Registration->Name==name?found->Instance:nullptr;
}

bool Runtime::StartGameModules(std::string& error) {
    _ActiveGameModules.reserve(_GameModuleTypes.size());
    for(const auto& [name,registration]:_GameModuleTypes)
        _ActiveGameModules.push_back({&registration});

    const auto run=[&](const char* phase,const auto& invoke) {
        for(auto& module:_ActiveGameModules) {
            RuntimeOperation operation(*this);
            std::string detail;
            bool succeeded=false;
            try {
                succeeded=invoke(module);
                if(!succeeded)detail="callback returned failure";
            } catch(const std::exception& exception) {detail=exception.what();}
              catch(...) {detail="callback threw an exception";}
            if(_ConstructionDepth) {
                _ConstructionDepth=0;
                if(succeeded)detail="callback left an open object construction scope";
                succeeded=false;
            }
            if(!succeeded) {
                error="Game module "+module.Registration->Name+" "+phase+": "+detail;
                return false;
            }
        }
        return true;
    };
    // Create the complete set before Initialize so every module can resolve its
    // peers. Run each phase across that set before proceeding to the next.
    return run("Create",[](auto& module) {
        module.Instance=module.Registration->Create();
        return module.Instance!=nullptr;
    }) && run("Initialize",[this](auto& module) {
        module.ReleaseNeeded=true;
        return module.Instance->Initialize(this);
    }) && run("Launch",[this](auto& module) {
        return module.Instance->Launch(this);
    }) && run("LaunchFinished",[this](auto& module) {
        module.Instance->LaunchFinished(this);
        return true;
    });
}

void Runtime::StopGameModules() noexcept {
    for(auto current=_ActiveGameModules.rbegin();current!=_ActiveGameModules.rend();++current) {
        if(!current->Instance)continue;
        RuntimeOperation operation(*this);
        const auto depth=_ConstructionDepth;
        const auto cleanup=[&](const char* phase,const auto& invoke) {
            try {invoke();}
            catch(const std::exception& exception) {NotifyGameModuleError(current->Registration->Name,phase,exception.what());}
            catch(...) {NotifyGameModuleError(current->Registration->Name,phase,"callback threw an exception");}
            if(_ConstructionDepth!=depth) {
                _ConstructionDepth=depth;
                NotifyGameModuleError(current->Registration->Name,phase,"callback left an unbalanced object construction scope");
            }
        };
        if(current->ReleaseNeeded)
            cleanup("Release",[&]{current->Instance->Release(this);});
        auto* instance=std::exchange(current->Instance,nullptr);
        cleanup("Destroy",[&]{current->Registration->Destroy(instance);});
    }
    _ActiveGameModules.clear();
}

void Runtime::TickGameModulePhase(std::uint8_t phase,float deltaSeconds) {
    if(!_ScriptExecutionEnabled||_ActiveGameModules.empty())return;
    using Callback=void (IModule::*)(IGameSproutRuntime*,float);
    Callback callback;
    const char* name;
    switch(phase) {
    case EarlyTick: callback=&IModule::EarlyUpdate;name="EarlyUpdate";break;
    case FixedTick: callback=&IModule::FixedUpdate;name="FixedUpdate";break;
    case UpdateTick: callback=&IModule::Update;name="Update";break;
    case LateTick: callback=&IModule::LateUpdate;name="LateUpdate";break;
    default: return;
    }
    RuntimeOperation operation(*this);
    for(auto& module:_ActiveGameModules) {
        if(module.Faulted)continue;
        const auto depth=_ConstructionDepth;
        const auto fail=[&](const char* message) {
            module.Faulted=true;
            NotifyGameModuleError(module.Registration->Name,name,message);
        };
        try {(module.Instance->*callback)(this,deltaSeconds);}
        catch(const std::exception& exception) {fail(exception.what());}
        catch(...) {fail("callback threw an exception");}
        if(_ConstructionDepth!=depth) {
            _ConstructionDepth=depth;
            if(!module.Faulted)fail("callback left an unbalanced object construction scope");
        }
    }
}

void Runtime::NotifyGameModuleError(const std::string& module,const char* phase,const char* error) noexcept {
    if(_ShuttingDown)return;
    try {
        SceneNodeEvent event;
        event.Type=SceneNodeEventType::OperationFailed;
        event.Name=module;
        event.ComponentType="GameSproutRuntime.IModule";
        event.Error="Game module "+module+" "+phase+": "+error;
        _Events.push_back({++_EventSequence,std::move(event)});
    } catch(...) {}
}
}

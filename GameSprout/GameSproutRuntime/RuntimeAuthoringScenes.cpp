#include "GameSproutRuntime.h"
#include "Scene.h"
#include <exception>

namespace GameSproutRuntime {
bool Runtime::ParkAuthoringScenes(const std::vector<std::uint64_t>& sceneIDs,std::string& error) {
    error.clear();
    if(IsExecuting()||_ShuttingDown||_ConstructionDepth||_ScriptExecutionEnabled||!_ParkedScenes.empty()) {
        error="Parking authoring scenes requires an idle Runtime before gameplay";
        return false;
    }
    if(sceneIDs.empty()||sceneIDs.size()>_Scenes.size()) {
        error="Parking requires a nonempty set of live authoring scenes";
        return false;
    }
    try {
        const std::set<std::uint64_t> requested(sceneIDs.begin(),sceneIDs.end());
        if(requested.size()!=sceneIDs.size()) {
            error="An authoring scene was requested twice";
            return false;
        }
        std::vector<Scene*> parked,remaining;
        parked.reserve(sceneIDs.size());
        remaining.reserve(_Scenes.size()-sceneIDs.size());
        for(auto* scene:_Scenes) {
            if(requested.contains(scene->GetInstanceID())&&IsInScene(&scene->State()))parked.push_back(scene);
            else remaining.push_back(scene);
        }
        if(parked.size()!=requested.size()) {
            error="An authoring scene is no longer live";
            return false;
        }

        // Pending initialization survives the switch. In particular a parked
        // component keeps its queued flag without being visited by Play frames.
        std::deque<InitializationRecord> parkedInitializations,remainingInitializations;
        for(const auto& record:_PendingInitializations) {
            const auto* node=Find(record.ObjectID);
            if(node&&node->SceneOwner&&requested.contains(node->SceneOwner->GetInstanceID()))
                parkedInitializations.push_back(record);
            else remainingInitializations.push_back(record);
        }

        if(requested.contains(_UIInputSceneID))SetUIInputViewport(nullptr,{},{});
        for(auto* scene:parked)scene->_ParkedForPlay=true;
        _Scenes.swap(remaining);
        _ParkedScenes.swap(parked);
        _PendingInitializations.swap(remainingInitializations);
        _ParkedInitializations.swap(parkedInitializations);
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="Cannot prepare authoring-scene parking";}
    return false;
}

bool Runtime::RestoreAuthoringScenes(std::string& error) {
    error.clear();
    if(_ParkedScenes.empty())return true;
    if(IsExecuting()||_ShuttingDown||_ConstructionDepth||_ScriptExecutionEnabled) {
        error="Restoring authoring scenes requires an idle Runtime after gameplay";
        return false;
    }
    try {
        if(!_Scenes.empty())_Scenes.reserve(_Scenes.size()+_ParkedScenes.size());
        if(!_ParkedInitializations.empty()) {
            auto initializations=_ParkedInitializations;
            initializations.insert(initializations.end(),_PendingInitializations.begin(),_PendingInitializations.end());
            _PendingInitializations.swap(initializations);
        }
        for(auto* scene:_ParkedScenes) {
            scene->_ParkedForPlay=false;
            scene->InvalidateVisual();
        }
        if(_Scenes.empty())_Scenes.swap(_ParkedScenes);
        else _Scenes.insert(_Scenes.begin(),_ParkedScenes.begin(),_ParkedScenes.end());
        _ParkedScenes.clear();
        _ParkedInitializations.clear();
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
      catch(...) {error="Cannot prepare authoring-scene restoration";}
    return false;
}
}

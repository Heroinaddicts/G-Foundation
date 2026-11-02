#pragma once
#include "ScriptEditor.h"
#include "ObjectEditing.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "GameSproutRuntimeExport.h"
#include "Internal/RuntimeScriptBindings.h"
#include <QCoreApplication>
#include <QThread>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const std::string& error) {if(!value)throw std::runtime_error(error);}
class FieldsBehaviour final:public GameSproutRuntime::ScriptBehaviour {
public:
    float Speed=1;
    GameSproutRuntime::IGameObject* Target=nullptr;
};
class SceneService final:public ISceneManager {
public:
    GameSproutRuntime::IScene* Scene=nullptr;
    bool Playing=false;
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    mutable uint64_t ComponentAllocations=0,NextComponentFileID=1000;
    mutable std::map<std::pair<uint64_t,uint64_t>,uint64_t> ComponentIDs;
    uint64_t GetComponentFileID(uint64_t objectID,uint64_t componentID) const override {
        auto* object=Runtime->QueryGameObject(objectID);if(!object||object->GetScene()!=Scene)return 0;
        const auto key=std::make_pair(objectID,componentID);
        if(!ComponentIDs.contains(key)){ComponentIDs[key]=++NextComponentFileID;++ComponentAllocations;}
        std::vector<GameSproutRuntime::SceneComponentIdentity> bindings;
        std::vector<GameSproutRuntime::IGameObject*> pending{Scene};
        for(std::size_t index=0;index<pending.size();++index){auto* item=pending[index];
            for(std::size_t c=0;c<item->GetComponentCount();++c)if(auto* component=item->GetComponentAt(c)){
                const auto entry=ComponentIDs.find({item->GetInstanceID(),component->GetComponentInstanceID()});
                if(entry!=ComponentIDs.end())bindings.push_back({component,entry->second});
            }
            for(std::size_t c=0;c<item->GetChildCount();++c)if(auto* child=item->GetChild(c))pending.push_back(child);
        }
        std::string error;return Runtime->SetSceneComponentIdentities(Scene,bindings,error)?ComponentIDs.at(key):0;
    }
    GameSproutRuntime::IComponent* QueryComponentByFileID(uint64_t sceneID,uint64_t id) const override {
        return Scene&&Scene->GetInstanceID()==sceneID?Runtime->QuerySceneComponentByFileID(Scene,id):nullptr;
    }

    fSceneEditOperation Apply,Revert;
    uint64_t EditedID=0;
    std::function<void()> Changed;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene()const override{return Scene;}
    const std::string& GetActiveScenePath()const override{return Path;}
    bool OpenScene(const std::string&,std::string&)override{return false;}
    bool IsPlaySessionActive()const override{return Playing;}
    bool EditObject(uint64_t id,const std::string&,const fSceneEditOperation& apply,const fSceneEditOperation& revert,std::string& error)override {
        GameSproutRuntime::IGameObject* found=nullptr;
        Scene->ForeachChild([&](auto* child){if(child->GetInstanceID()==id)found=child;});
        if(!found)return false;
        if(!apply(*found,error))return false;
        Apply=apply;Revert=revert;EditedID=id;if(Changed)Changed();return true;
    }
    bool Undo(std::string& error)override {return Revert&&Revert(*Owner(),error);}
    bool Redo(std::string& error)override {return Apply&&Apply(*Owner(),error);}
    bool CanUndo()const override{return bool(Revert);}
    bool CanRedo()const override{return bool(Apply);}
    bool SaveScene(std::string&)override{return false;}
    bool SaveSceneAs(const std::string&,std::string&)override{return false;}
    bool HasUnsavedChanges()const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&)override{return false;}
    bool RegisterSceneListener(ISceneListener*)override{return true;}
    void UnregisterSceneListener(ISceneListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
private:
    GameSproutRuntime::IGameObject* Owner()const {
        for(std::size_t i=0;i<Scene->GetChildCount();++i)if(Scene->GetChild(i)->GetInstanceID()==EditedID)return Scene->GetChild(i);
        return nullptr;
    }
};
class SelectionService final:public ISelectionManager {
public:
    SceneObjectSelection SceneSelection;
    std::vector<ProjectAssetSelection> GetAssetSelection()const override{return {};}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>&)override{}
    SceneObjectSelection GetSceneObjectSelection()const override{return SceneSelection;}
    void SetSceneObjectSelection(const SceneObjectSelection& value)override{SceneSelection=value;}
    bool RegisterSelectionListener(ISelectionListener*)override{return true;}
    void UnregisterSelectionListener(ISelectionListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Listener final:public IScriptEditorListener {
public:
    int Changes=0;
    void OnScriptFieldsChanged(const ScriptFieldTarget&)override{++Changes;}
};
class ModeService final:public IPlayMode {
public:
    PlayModeState State;
    PlayModeState GetState()const override{return State;}
    bool Play(std::string&)override{return false;}bool Pause(std::string&)override{return false;}bool Resume(std::string&)override{return false;}
    bool Step(std::string&)override{return false;}bool Stop(std::string&)override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener*)override{return true;}void UnregisterPlayModeListener(IPlayModeListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class HostService final:public IRuntimeHost {
public:
    bool Frame=false;bool IsFrameActive()const override{return Frame;}
    void SetSimulationPlaying(bool)override{}bool IsSimulationPlaying()const override{return false;}void ResetClock()override{}
    bool RegisterFrameListener(IRuntimeFrameListener*)override{return true;}void UnregisterFrameListener(IRuntimeFrameListener*)override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};

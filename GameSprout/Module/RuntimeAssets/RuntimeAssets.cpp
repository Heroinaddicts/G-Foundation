#include "RuntimeAssets.h"
#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include <limits>
RuntimeAssets::~RuntimeAssets(){Release(nullptr);}
void RuntimeAssets::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api){
    if(!api||!api->_EditorApi)return;
    _Assets=api->_EditorApi->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library=api->_EditorApi->GetGameSproutModule<ILibraryManager>("LibraryManager");
}
bool RuntimeAssets::Initialize(GameSproutEditor::GameSproutModuleApi*){
    if(!_Assets||!_Library)return false;
    if(!_Resources) {
        _Resources=new LibraryRuntimeResources(_Assets,_Library);
        _Resources->ResourcesChanged=[this]{NotifyResourcesChanged();};
    }
    return true;
}
GameSproutRuntime::IRuntimeResourceProvider* RuntimeAssets::GetResourceProvider() const{return _Resources;}
void RuntimeAssets::Release(GameSproutEditor::GameSproutModuleApi*){
    _Listeners.clear();if(_Resources)_Resources->ResourcesChanged={};
    delete _Resources;_Resources=nullptr;_Assets=nullptr;_Library=nullptr;
}
bool RuntimeAssets::RegisterResourceListener(IRuntimeAssetsListener* listener) {
    if(!listener||_NextListener==std::numeric_limits<std::uint64_t>::max())return false;
    for(const auto& [id,registered]:_Listeners)if(registered==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void RuntimeAssets::UnregisterResourceListener(IRuntimeAssetsListener* listener) {
    for(auto found=_Listeners.begin();found!=_Listeners.end();)
        if(found->second==listener)found=_Listeners.erase(found);else ++found;
}
void RuntimeAssets::NotifyResourcesChanged() {
    const auto revision=_Resources?_Resources->GetRevision():0;
    std::vector<std::uint64_t> listeners;listeners.reserve(_Listeners.size());
    for(const auto& [id,listener]:_Listeners)listeners.push_back(id);
    for(const auto id:listeners) {
        const auto found=_Listeners.find(id);
        if(found!=_Listeners.end())try{found->second->OnRuntimeResourcesChanged(revision);}catch(...){}
    }
}

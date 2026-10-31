#include "RuntimeAssets.h"
#include "LibraryRuntimeResources.h"
#include "IProject.h"
#include "ILibraryManager.h"
RuntimeAssets::~RuntimeAssets(){Release(nullptr);}
void RuntimeAssets::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api){
    if(!api||!api->_EditorApi)return;
    _Assets=api->_EditorApi->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library=api->_EditorApi->GetGameSproutModule<ILibraryManager>("LibraryManager");
    _Project=api->_EditorApi->GetGameSproutModule<IProject>("Project");
}
bool RuntimeAssets::Initialize(GameSproutEditor::GameSproutModuleApi*){
    if(!_Assets||!_Library||!_Project)return false;
    if(!_Resources)_Resources=new LibraryRuntimeResources(_Assets,_Library);
    return true;
}
GameSproutRuntime::IRuntimeResourceProvider* RuntimeAssets::GetResourceProvider() const{return _Resources;}
void RuntimeAssets::Release(GameSproutEditor::GameSproutModuleApi*){
    delete _Resources;_Resources=nullptr;_Assets=nullptr;_Library=nullptr;_Project=nullptr;
}

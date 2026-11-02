#include "PhysicsAuthoring.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProject.h"

PhysicsAuthoring::~PhysicsAuthoring(){Release(nullptr);}
void PhysicsAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    g_Self=this;g_GameSproutApi=api;
    auto* editor=api?api->_EditorApi:nullptr;
    if(!editor)return;
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library=editor->GetGameSproutModule<ILibraryManager>("LibraryManager");
    _Project=editor->GetGameSproutModule<IProject>("Project");
}
bool PhysicsAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    return _Assets&&_Library&&_Project;
}
void PhysicsAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Assets=nullptr;_Library=nullptr;_Project=nullptr;
    if(g_Self==this)g_Self=nullptr;g_GameSproutApi=nullptr;
}

#include "PhysicsAuthoring.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "ISceneManager.h"
#include "IObjectEditing.h"
#include <QThread>

PhysicsAuthoring::~PhysicsAuthoring(){Release(nullptr);}
void PhysicsAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    g_Self=this;g_GameSproutApi=api;
    auto* editor=api?api->_EditorApi:nullptr;
    if(!editor)return;
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Library=editor->GetGameSproutModule<ILibraryManager>("LibraryManager");
    _Project=editor->GetGameSproutModule<IProject>("Project");
    _Scenes=editor->GetGameSproutModule<ISceneManager>("SceneManager");
    _Objects=editor->GetGameSproutModule<IObjectEditing>("ObjectEditing");
    _Runtime=editor->_Runtime;
}
bool PhysicsAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();
    return _Assets&&_Library&&_Project;
}
void PhysicsAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    std::string ignored;CancelColliderEdit(ignored);
    _Assets=nullptr;_Library=nullptr;_Project=nullptr;
    _Scenes=nullptr;_Objects=nullptr;_Runtime=nullptr;_OwnerThread=nullptr;
    if(g_Self==this)g_Self=nullptr;g_GameSproutApi=nullptr;
}

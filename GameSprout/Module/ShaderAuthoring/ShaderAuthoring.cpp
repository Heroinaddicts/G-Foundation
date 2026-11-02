#include "ShaderAuthoring.h"
#include "IProject.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include <QThread>

ShaderAuthoring::~ShaderAuthoring(){Release(nullptr);}
void ShaderAuthoring::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    g_ShaderAuthoring=this;g_ShaderAuthoringApi=api;
    auto* editor=api?api->_EditorApi:nullptr;
    _Project=editor?editor->GetGameSproutModule<IProject>("Project"):nullptr;
    _Assets=editor?editor->GetGameSproutModule<IAssetsManager>("AssetsManager"):nullptr;
    _Library=editor?editor->GetGameSproutModule<ILibraryManager>("LibraryManager"):nullptr;
}
bool ShaderAuthoring::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    _OwnerThread=QThread::currentThread();return _Project&&_Assets&&_Library;
}
bool ShaderAuthoring::Launch(GameSproutEditor::GameSproutModuleApi*) {return true;}
void ShaderAuthoring::LaunchFinished(GameSproutEditor::GameSproutModuleApi*) {}
void ShaderAuthoring::Update(GameSproutEditor::GameSproutModuleApi*) {}
void ShaderAuthoring::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Project=nullptr;_Assets=nullptr;_Library=nullptr;_OwnerThread=nullptr;
    if(g_ShaderAuthoring==this)g_ShaderAuthoring=nullptr;g_ShaderAuthoringApi=nullptr;
}

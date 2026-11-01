#include "AnimationEditor.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "IRuntimeAssets.h"

AnimationEditor::~AnimationEditor(){Release(nullptr);}
void AnimationEditor::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    auto* editor=api?api->_EditorApi:nullptr;
    _Assets=editor?editor->GetGameSproutModule<IAssetsManager>("AssetsManager"):nullptr;
    _Library=editor?editor->GetGameSproutModule<ILibraryManager>("LibraryManager"):nullptr;
    _Project=editor?editor->GetGameSproutModule<IProject>("Project"):nullptr;
    _RuntimeAssets=editor?editor->GetGameSproutModule<IRuntimeAssets>("RuntimeAssets"):nullptr;
}
bool AnimationEditor::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    return _Assets&&_Library&&_Project&&_RuntimeAssets;
}
bool AnimationEditor::Launch(GameSproutEditor::GameSproutModuleApi*) {
    // Every module completes Initialize before any Launch. RuntimeAssets owns
    // the provider; it need not exist in alphabetical Initialize ordering.
    _Resources=_RuntimeAssets?_RuntimeAssets->GetResourceProvider():nullptr;
    return _Resources!=nullptr;
}
void AnimationEditor::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Resources=nullptr;_RuntimeAssets=nullptr;_Assets=nullptr;_Library=nullptr;_Project=nullptr;
}

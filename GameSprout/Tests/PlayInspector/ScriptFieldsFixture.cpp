#include "ScriptEditor.h"
// Fields-only integration fixture. Compiler/task orchestration is independently
// tested by NativeScripts; here the production property service is wired directly.
IScriptEditor* CreateFieldsFixture(GameSproutRuntime::IGameSproutRuntime* runtime,ISceneManager* scenes,ISelectionManager* selection,IObjectEditing* editing,const GameSproutRuntime::ScriptTypeInfo& type) {
    auto* scripts=new ScriptEditor();scripts->_Runtime=runtime;scripts->_Scenes=scenes;scripts->_Selection=selection;scripts->_Editing=editing;scripts->_Launched=true;
    scripts->_Types.push_back({type,{type.TypeID,1},"Fixture.hpp"});
    scripts->_FieldSceneSubscribed=scenes->RegisterSceneListener(scripts);
    scripts->_FieldRuntimeSubscribed=runtime->RegisterListener(scripts);
    return scripts;
}
void DestroyFieldsFixture(IScriptEditor* scripts){auto* concrete=static_cast<ScriptEditor*>(scripts);concrete->Release(nullptr);delete concrete;}

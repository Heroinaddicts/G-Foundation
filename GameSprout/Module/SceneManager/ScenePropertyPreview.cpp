#include "SceneManager.h"

bool SceneManager::BeginPropertyPreview(std::string& error) {
    if(!Ready(error))return false;
    Json::Value document;std::string text;if(!Capture(document,text,error))return false;
    _PropertyPreview=true;return true;
}
void SceneManager::EndPropertyPreview() {_PropertyPreview=false;}
void SceneManager::PublishPropertyPreview() {
    if(_PropertyPreview&&Runtime()&&!Runtime()->IsExecuting()&&!_Switching&&!_ExecutingEdit&&!_Notifying)
        RefreshState(true);
}
bool SceneManager::ValidatePropertyPreview(std::string& error) const {
    if(!_PropertyPreview){error="没有活动的属性预览。";return false;}
    Json::Value document;std::string text;return Capture(document,text,error);
}

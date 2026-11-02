#include "GameSproutRuntime.h"
#include "Api/RuntimeResourceApi.h"
#include "PrefabDocument.h"

namespace GameSproutRuntime {
IGameObject* Runtime::InstantiatePrefabData(IScene* scene,const PrefabData& data,IGameObject* parent,
    std::vector<PrefabObjectMapping>& mappings,std::string& error) {
    error.clear();
    if(IsExecuting()||!scene||QueryGameObject(scene->GetInstanceID())!=scene) {
        error="Prefab requires a live scene outside Runtime callbacks";return nullptr;
    }
    if(!parent)parent=scene;
    if(QueryGameObject(parent->GetInstanceID())!=parent||parent->GetScene()!=scene) {
        error="Prefab parent must belong to the target scene";return nullptr;
    }
    try {
        Json::Value document;
        if(!PrefabDocument::Decode(data.Bytes,document,error))return nullptr;
        IGameObject* output=nullptr;
        return SceneDocument::InstantiatePrefab(document,*this,*scene,*parent,output,mappings,error)?output:nullptr;
    }catch(const std::exception& exception){error=exception.what();}
    catch(...){error="Prefab binary decoding failed";}
    return nullptr;
}
IGameObject* Runtime::InstantiatePrefab(IScene* scene,const ResourceReference& reference,IGameObject* parent,
    std::vector<PrefabObjectMapping>& mappings,std::string& error) {
    if(IsExecuting()){error="Cannot load a Prefab inside a Runtime callback";return nullptr;}
    auto* provider=GetResourceProvider();
    if(!provider){error="Prefab requires a Runtime resource provider";return nullptr;}
    PrefabData data;
    try {
        if(!provider->LoadPrefab(reference,data,error)) {
            if(error.empty())error="Runtime resource provider could not load the Prefab";
            return nullptr;
        }
        return InstantiatePrefabData(scene,data,parent,mappings,error);
    }catch(const std::exception& exception){error=exception.what();}
    catch(...){error="Prefab resource provider failed";}
    return nullptr;
}
}

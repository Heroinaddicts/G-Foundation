#pragma once

static GameSproutRuntime::IScriptComponent* ScriptAt(GameSproutRuntime::IGameObject& object,std::size_t index) {
    for(std::size_t i=0;i<object.GetComponentCount();++i)
        if(auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object.GetComponentAt(i))) {
            if(!index--)return script;
        }
    return nullptr;
}
static void CheckComponentTransactions(SceneManager& manager,const QDir& root,GameSproutRuntime::IGameSproutRuntime* runtime) {
    std::string path,error;
    Check(manager.CreateSceneAsset("Scenes","Components",path,error)&&manager.OpenScene(path,error),error);
    auto* scene=manager.GetActiveScene();auto* object=scene->CreateGameObject("Component target");
    const auto objectID=object->GetInstanceID();
    auto* transform=object->GetTransform();
    auto* image=object->AddComponent<GameSproutRuntime::IImageComponent>();image->SetColor({.1f,.2f,.3f,.4f});
    auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();
    Check(manager.SaveScene(error),error);
    const auto filename=root.filePath("Assets/"+QString::fromStdString(path));
    const auto before=Read(filename);
    Check(!manager.RemoveComponent(objectID,"GameSproutRuntime.Transform",error)&&
        !manager.AddBuiltinComponent(objectID,"GameSproutRuntime.Image",error)&&!manager.CanUndo(),
        "Intrinsic/duplicate component operation created history");
    Check(manager.RemoveComponent(objectID,"GameSproutRuntime.Image",error)&&!object->GetComponent<GameSproutRuntime::IImageComponent>()&&
        object->GetComponent<GameSproutRuntime::IMeshComponent>()==mesh&&object->GetTransform()==transform,
        "Component removal replaced an untouched component");
    Check(manager.Undo(error)&&object->GetComponentAt(1)==object->GetComponent<GameSproutRuntime::IImageComponent>()&&
        object->GetComponentAt(2)==mesh&&!manager.HasUnsavedChanges(),"Undo did not restore component order and clean baseline: "+error);
    Check(manager.SaveScene(error)&&Read(filename)==before,"Component Undo changed serialized data");
    Check(manager.AddBuiltinComponent(objectID,"GameSproutRuntime.Camera",error)&&!manager.CanRedo(),error);
    Check(scene->SetMainCamera(object->GetComponent<GameSproutRuntime::ICameraComponent>())&&manager.SaveScene(error),error);
    Check(manager.RemoveComponent(objectID,"GameSproutRuntime.Camera",error)&&!scene->GetMainCamera(),error);
    Check(manager.Undo(error)&&scene->GetMainCamera()==object->GetComponent<GameSproutRuntime::ICameraComponent>()&&!manager.HasUnsavedChanges(),
        "Camera component Undo lost the main camera or baseline: "+error);
    Check(manager.Redo(error)&&!scene->GetMainCamera()&&manager.Undo(error),error);

    const GameSproutRuntime::ResourceReference source{"1234567890abcdef1234567890abcdef",1};
    Check(manager.AttachScriptComponent(objectID,source.Guid,source,error),error);
    auto* first=ScriptAt(*object,0);Check(first&&!first->IsScriptTypeAvailable(),"Missing script type was not preserved as an authoring wrapper");
    const auto firstID=first->GetScriptInstanceID();
    GameSproutRuntime::ScriptValue speed;speed.Kind=GameSproutRuntime::ScriptValueKind::Number;speed.Number=2.5;
    GameSproutRuntime::ScriptValue texture;texture.Kind=GameSproutRuntime::ScriptValueKind::Resource;
    texture.Resource={"abcdef0123456789abcdef0123456789",37};
    Check(first->SetField("speed",speed,error)&&first->SetField("texture",texture,error),error);
    GameSproutRuntime::ScriptValue boolean;boolean.Kind=GameSproutRuntime::ScriptValueKind::Boolean;boolean.Boolean=true;
    GameSproutRuntime::ScriptValue integer;integer.Kind=GameSproutRuntime::ScriptValueKind::Integer;integer.Integer=std::numeric_limits<std::int64_t>::min();
    GameSproutRuntime::ScriptValue text;text.Kind=GameSproutRuntime::ScriptValueKind::String;text.String="中文用户脚本";
    GameSproutRuntime::ScriptValue vector;vector.Kind=GameSproutRuntime::ScriptValueKind::Vector3;vector.Vector={1.25f,-2.5f,3.75f};
    Check(first->SetField("boolean",boolean,error)&&first->SetField("integer",integer,error)&&
        first->SetField("text",text,error)&&first->SetField("vector",vector,error),error);
    first->SetEnabled(false);
    const auto beforeDuplicate=object->GetComponentCount();
    Check(!manager.AttachScriptComponent(objectID,source.Guid,source,error)&&object->GetComponentCount()==beforeDuplicate,
        "Duplicate native script type was attached to one object");
    const GameSproutRuntime::ResourceReference secondSource{"2234567890abcdef1234567890abcdef",1};
    Check(manager.AttachScriptComponent(objectID,secondSource.Guid,secondSource,error),error);
    auto* second=ScriptAt(*object,1);Check(second&&second->GetScriptInstanceID()!=firstID,"Script instances share an identity");
    const auto secondID=second->GetScriptInstanceID();
    Check(manager.SaveScene(error),error);const auto scriptSnapshot=Read(filename);
    Check(manager.RemoveScriptComponent(objectID,firstID,error)&&ScriptAt(*object,0)==second&&manager.Undo(error),error);
    first=ScriptAt(*object,0);
    Check(first&&first->GetScriptInstanceID()==firstID&&!first->IsEnabled()&&first->GetFields().at("speed")==speed&&
        first->GetFields().at("texture")==texture&&ScriptAt(*object,1)==second&&!manager.HasUnsavedChanges(),
        "Script Undo lost authored values, identity, order or other instance: "+error);
    Check(manager.SaveScene(error)&&Read(filename)==scriptSnapshot,"Script Undo changed the .scene source");
    const auto fileID=manager.GetObjectFileID(objectID);
    Check(manager.DeleteObject(objectID,error)&&manager.Undo(error),error);
    object=manager.QueryObjectByFileID(fileID);first=object?ScriptAt(*object,0):nullptr;second=object?ScriptAt(*object,1):nullptr;
    Check(first&&second&&first->GetScriptInstanceID()==firstID&&second->GetScriptInstanceID()==secondID,
        "Deleted subtree Undo did not restore script identities");
    Check(manager.OpenScene(path,error),error);
    object=manager.GetActiveScene()->FindChild("Component target");first=object?ScriptAt(*object,0):nullptr;
    Check(first&&first->GetScriptTypeID()==source.Guid&&first->GetScriptSource()==source&&
        first->GetFields().at("speed")==speed&&first->GetFields().at("texture")==texture&&
        first->GetFields().at("boolean")==boolean&&first->GetFields().at("integer")==integer&&
        first->GetFields().at("text")==text&&first->GetFields().at("vector")==vector&&
        !first->IsEnabled()&&ScriptAt(*object,1)->GetScriptInstanceID()==secondID&&
        ScriptAt(*object,1)->GetScriptTypeID()==secondSource.Guid&&ScriptAt(*object,1)->GetScriptSource()==secondSource,
        "Scene reload did not restore missing native scripts and values: "+error);
    auto document=Document(filename);
    for(auto& node:document["Objects"])if(node["Name"]=="Component target") {
        auto& components=node["Components"];
        components.append(components[components.size()-1]);
    }
    Check(!SceneDocument::Validate(document,error),"Duplicate script instance identity was accepted");
    auto duplicateType=Document(filename);
    for(auto& node:duplicateType["Objects"])if(node["Name"]=="Component target") {
        Json::Value firstScript;
        for(auto& component:node["Components"])if(component["Type"]=="GameSproutRuntime.ScriptComponent") {
            if(firstScript.isNull())firstScript=component;
            else {component["ScriptTypeID"]=firstScript["ScriptTypeID"];break;}
        }
    }
    Check(!SceneDocument::Validate(duplicateType,error),"One object accepted two script wrappers of the same native type");
    for(const auto& kind:std::vector<std::string>{"Unknown","Number"}) {
        auto bad=Document(filename);
        for(auto& node:bad["Objects"])if(node["Name"]=="Component target")
            for(auto& component:node["Components"])if(component["Type"]=="GameSproutRuntime.ScriptComponent") {
                component["Fields"]["speed"]["Kind"]=kind;
                if(kind=="Number")component["Fields"]["speed"]["Value"]=std::numeric_limits<double>::infinity();
            }
        Check(!SceneDocument::Validate(bad,error),"Invalid script field data was accepted");
    }
    auto* foreign=runtime->CreateScene("Component transaction foreign");auto* node=foreign->CreateGameObject("foreign");
    Check(!manager.AddBuiltinComponent(node->GetInstanceID(),"GameSproutRuntime.Mesh",error)&&runtime->DestroyScene(foreign),
        "Component transaction accepted a foreign scene node");

    const auto animatorOwnerID=object->GetInstanceID();
    Check(manager.AddBuiltinComponent(animatorOwnerID,"GameSproutRuntime.Animator",error),error);
    auto* animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    auto* bone=manager.GetActiveScene()->CreateGameObject("Bound bone",object);
    const auto boneID=bone->GetInstanceID();
    Check(animator->SetNodeBindings({{"Root/Bone",boneID}}),"Cannot establish component binding fixture");
    Check(manager.AddBuiltinComponent(boneID,"GameSproutRuntime.SkinnedMeshRenderer",error),error);
    Check(bone->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>()->SetAnimatorObjectID(animatorOwnerID),
        "Cannot establish skin/Animator fixture");
    Check(manager.SaveScene(error),error);
    Check(!manager.RemoveComponent(animatorOwnerID,"GameSproutRuntime.Animator",error)&&
        object->GetComponent<GameSproutRuntime::IAnimatorComponent>()==animator,
        "Removing a referenced Animator left a dangling skin binding");
    Check(manager.RemoveComponent(boneID,"GameSproutRuntime.SkinnedMeshRenderer",error)&&manager.Undo(error),error);
    Check(bone->GetComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>()->GetAnimatorObjectID()==animatorOwnerID&&
        !manager.HasUnsavedChanges(),"Skin component Undo lost its Animator or clean baseline");
    Check(manager.Redo(error)&&manager.RemoveComponent(animatorOwnerID,"GameSproutRuntime.Animator",error)&&manager.Undo(error),error);
    animator=object->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    Check(animator&&animator->GetNodeBindings().size()==1&&animator->GetNodeBindings()[0].ObjectID==boneID,
        "Animator component Undo lost stable-file node bindings");
    Check(manager.Undo(error)&&!manager.HasUnsavedChanges(),"Component binding Undo did not restore the original scene");
}

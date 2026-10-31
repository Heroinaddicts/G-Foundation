#include "SceneDocument.h"
#include "GameSproutRuntimeExport.h"
#include <iostream>
#include <limits>
#include <stdexcept>

static void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static Json::Value Vector(std::initializer_list<float> list) {
    Json::Value result(Json::arrayValue);
    for (float value:list) result.append(value);
    return result;
}
static Json::Value Node(std::uint64_t id, std::uint64_t parent, const char* name) {
    Json::Value result(Json::objectValue);
    result["ID"]=Json::UInt64(id);
    result["ParentID"]=Json::UInt64(parent);
    result["Name"]=name;
    result["Active"]=true;
    result["Components"]=Json::Value(Json::arrayValue);
    return result;
}
static Json::Value Component(const char* name) {
    Json::Value result(Json::objectValue);
    result["Type"]=std::string("GameSproutRuntime.")+name;
    return result;
}
class InstantiationProbe final : public GameSproutRuntime::IRuntimeListener {
public:
    std::function<void()> Action;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {
        if (Action) Action();
    }
};
static Json::Value AllComponents(const Json::Value& initial) {
    Json::Value source=initial;
    source["Name"]="All components";
    source["MainCameraID"]=Json::UInt64(30);
    source["Objects"]=Json::Value(Json::arrayValue);

    // File ID 30 precedes parent ID 20. Runtime instance IDs need not match these IDs.
    Json::Value object=Node(30,20,"First");
    Json::Value transform=Component("Transform");
    transform["Position"]=Vector({1,2,3});
    transform["Scale"]=Vector({-1,2,3});
    object["Components"].append(transform);
    Json::Value rect=Component("RectTransform");
    rect["SizeDelta"]=Vector({-10,25});
    object["Components"].append(rect);
    Json::Value image=Component("Image");
    image["Texture"]["Guid"]="ABCDEF0123456789ABCDEF0123456789";
    image["Texture"]["LocalID"]=Json::UInt64(7);
    image["Color"]=Vector({2,1,0,0.5f});
    object["Components"].append(image);
    Json::Value canvas=Component("Canvas");
    canvas["RenderMode"]="WorldSpace";
    canvas["PixelsPerUnit"]=50;
    object["Components"].append(canvas);
    object["Components"].append(Component("Mesh")); // Omitted reference means unbound.
    Json::Value renderer=Component("MeshRenderer");
    renderer["Materials"]=Json::Value(Json::arrayValue);
    renderer["Materials"].append(Json::Value()); // Preserve an unbound material slot.
    renderer["Materials"].append(image["Texture"]);
    object["Components"].append(renderer);
    Json::Value camera=Component("Camera");
    camera["Settings"]["Projection"]="Orthographic";
    camera["Settings"]["NearPlane"]=0;
    object["Components"].append(camera);
    source["Objects"].append(object);
    source["Objects"].append(Node(20,0,"Parent"));
    source["Objects"].append(Node(40,20,"Second"));
    return source;
}

#include "SceneSerializationChecks.h"

int main() {
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try {
        // Link to the independently built SDK; no private Runtime headers are used.
        runtime=CreateGameSproutRuntime();
        Check(runtime!=nullptr,"Real Runtime factory unavailable; build GameSproutRuntime first");
        GameSproutRuntime::IScene* output=nullptr;
        std::string error;
        const auto initial=SceneDocument::CreateDefault("Default");
        const bool created=SceneDocument::Instantiate(initial,*runtime,output,error);
        Check(created,error);
        Check(output->GetChildCount()==1 && output->GetMainCamera() &&
            output->GetMainCamera()->GetGameObject()->GetTransform()->GetLocalPosition().Z==3,
            "Default camera mismatch");

        const auto source=AllComponents(initial);
        const bool imported=SceneDocument::Instantiate(source,*runtime,output,error);
        Check(imported,error);
        auto* parent=output->GetChild(0);
        Check(parent->GetName()=="Parent" && parent->GetChildCount()==2 &&
            parent->GetChild(0)->GetName()=="First" && parent->GetChild(1)->GetName()=="Second",
            "Child-before-parent source order was lost");
        auto* object=parent->GetChild(0);
        Check(object->GetComponentCount()==7,"Not all builtin components were instantiated");
        Check(output->GetMainCamera()==object->GetComponent<GameSproutRuntime::ICameraComponent>() &&
            object->GetInstanceID()!=30,"MainCameraID was confused with Runtime InstanceID");
        Check(object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture().Guid==
            "abcdef0123456789abcdef0123456789","Resource GUID normalization failed");
        Check(object->GetComponent<GameSproutRuntime::IImageComponent>()->GetTexture().LocalID==7,
            "Resource LocalID was lost");
        Check(object->GetComponent<GameSproutRuntime::ICanvasComponent>()->GetPixelsPerUnit()==50,
            "Canvas settings were lost");
        Check(object->GetComponent<GameSproutRuntime::IRectTransformComponent>()->GetSizeDelta().X==-10,
            "Negative stretch size delta was rejected");
        Check(object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->GetMaterialCount()==2,
            "Material slot order was lost");

        auto* saved=output;
        const auto count=runtime->GetSceneCount();
        const auto reject=[&](const Json::Value& bad) {
            Check(!SceneDocument::Instantiate(bad,*runtime,output,error),"Invalid document was accepted");
            Check(!error.empty(),"Invalid document has no diagnostic");
            Check(output==saved && runtime->GetSceneCount()==count,"Failure changed output or leaked candidate scene");
        };
        auto bad=source; bad["Version"]=2; reject(bad);
        bad=source; bad["Name"]=""; reject(bad);
        bad=source; bad["Unknown"]=true; reject(bad);
        bad=source; bad["Objects"][1]["ParentID"]=Json::UInt64(30); reject(bad); // Cycle.
        bad=source; bad["Objects"][1]["ID"]=Json::UInt64(30); reject(bad); // Duplicate.
        bad=source; bad["Objects"][0]["ID"]=Json::UInt64(0); reject(bad);
        bad=source; bad["Objects"][0]["ParentID"]=Json::UInt64(123); reject(bad);
        bad=source; bad["Objects"][0]["Components"].append(Component("Image")); reject(bad);
        bad=source; bad["Objects"][0]["Components"][0]["Scale"]=Vector({1,2}); reject(bad);
        bad=source; bad["Objects"][0]["Components"][0]["Rotation"]=Vector({0,0,0,0}); reject(bad);
        bad=source; bad["Objects"][0]["Components"][2]["Texture"]["Guid"]=""; reject(bad);
        bad=source; bad["Objects"][0]["Components"][2]["Texture"]["LocalID"]=Json::UInt64(0); reject(bad);
        bad=source; bad["MainCameraID"]=Json::UInt64(20); reject(bad); // No Camera on parent.
        bad=source; bad["Objects"][0]["Components"][0]["Type"]="UnknownComponent"; reject(bad);
        bad=source; bad["Objects"][0]["Active"]=1; reject(bad);
        bad=source; bad["Objects"][0]["Components"][0]["Position"][0]=std::numeric_limits<double>::infinity(); reject(bad);
        bad=source; bad["Objects"][0]["Components"][6]["Settings"]["NearPlane"]=-1; reject(bad);
        bad=source; bad["Objects"][0]["Components"][5]["Materials"].resize(1025); reject(bad);

        // Whole-document validation succeeds, then overflow forces rollback after creation.
        bad=source;
        bad["Objects"][0]["Components"][0]["Position"]=Vector({std::numeric_limits<float>::max(),0,0});
        auto huge=Component("Transform");
        huge["Scale"]=Vector({std::numeric_limits<float>::max(),1,1});
        bad["Objects"][1]["Components"].append(huge);
        reject(bad);

        bad=source; bad["Objects"]=Json::Value(Json::arrayValue); bad["MainCameraID"]=Json::UInt64(0);
        for (std::uint64_t id=1;id<=1025;++id) bad["Objects"].append(Node(id,id-1,"Depth"));
        reject(bad);
        bad=source; bad["Objects"]=Json::Value(Json::arrayValue); bad["Objects"].resize(100001); reject(bad);

        // 新场景导入必须在 Runtime 的结构屏障之外进行，否则候选可能尚未实际挂载。
        bool visited=false, rejectedInTraversal=false, traversalPreserved=false, executingInTraversal=false;
        output->ForeachChild([&](GameSproutRuntime::IGameObject*) {
            visited=true;
            executingInTraversal=runtime->IsExecuting();
            rejectedInTraversal=!SceneDocument::Instantiate(initial,*runtime,output,error);
            traversalPreserved=output==saved && runtime->GetSceneCount()==count;
        });
        Check(visited && executingInTraversal && rejectedInTraversal && traversalPreserved && !runtime->IsExecuting(),
            "Instantiation inside node traversal was not rejected atomically");
        InstantiationProbe probe;
        bool called=false, rejectedInListener=false, listenerPreserved=false, executingInListener=false;
        probe.Action=[&]() {
            called=true;
            executingInListener=runtime->IsExecuting();
            rejectedInListener=!SceneDocument::Instantiate(initial,*runtime,output,error);
            listenerPreserved=output==saved && runtime->GetSceneCount()==count;
        };
        Check(runtime->RegisterListener(&probe),"Cannot register instantiation reentrancy probe");
        output->SetName("Listener instantiation probe");
        runtime->Update(0);
        runtime->UnregisterListener(&probe);
        Check(called && executingInListener && rejectedInListener && listenerPreserved && !runtime->IsExecuting(),
            "Instantiation inside Runtime listener was not rejected atomically");

        auto empty=source;
        empty["Objects"]=Json::Value(Json::arrayValue); empty["MainCameraID"]=Json::UInt64(0);
        const bool emptyCreated=SceneDocument::Instantiate(empty,*runtime,output,error);
        Check(emptyCreated,error);
        Check(output->GetChildCount()==0 && !output->GetMainCamera(),"Empty scene failed");
        CheckSerialization(*runtime,source);
        delete runtime;
        runtime=nullptr;
        std::cout << "PASS real Runtime: default scene/main camera, seven components, file IDs/order, validation, rollback and limits\n";
        return 0;
    } catch (const std::exception& exception) {
        delete runtime;
        std::cerr << "FAIL " << exception.what() << '\n';
        return 1;
    }
}

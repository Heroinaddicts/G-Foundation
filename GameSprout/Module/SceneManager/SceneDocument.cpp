#include "SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::size_t MaximumObjects = 100000;
constexpr std::size_t MaximumDepth = 1024;
constexpr std::size_t MaximumMaterials = 1024;
constexpr std::size_t MaximumNameBytes = 1024;

[[noreturn]] void Fail(const std::string& path, const std::string& reason) {
    throw std::runtime_error(path + ": " + reason);
}
void Members(const Json::Value& value, const std::string& path, std::initializer_list<const char*> allowed) {
    if (!value.isObject()) Fail(path,"expected object");
    for (const std::string& key : value.getMemberNames()) {
        if (std::none_of(allowed.begin(),allowed.end(),[&](const char* name){return key==name;}))
            Fail(path + "." + key,"unknown field");
    }
}
std::uint64_t Unsigned(const Json::Value& value, const std::string& path) {
    if ((value.type()!=Json::intValue && value.type()!=Json::uintValue) || !value.isUInt64())
        Fail(path,"expected unsigned integer");
    return value.asUInt64();
}
std::string Name(const Json::Value& value, const std::string& path) {
    if (!value.isString()) Fail(path,"expected string");
    const std::string result=value.asString();
    if (result.empty() || result.size()>MaximumNameBytes || result.find('\0')!=std::string::npos)
        Fail(path,"name must contain 1..1024 bytes without NUL");
    return result;
}
float Number(const Json::Value& value, const std::string& path) {
    if (!value.isNumeric()) Fail(path,"expected number");
    const double result=value.asDouble();
    if (!std::isfinite(result) || std::fabs(result)>std::numeric_limits<float>::max())
        Fail(path,"number is not a finite float");
    return static_cast<float>(result);
}
float FloatMember(const Json::Value& value, const char* name, float fallback, const std::string& path) {
    return value.isMember(name)?Number(value[name],path+"."+name):fallback;
}
bool BoolMember(const Json::Value& value, const char* name, bool fallback, const std::string& path) {
    if (!value.isMember(name)) return fallback;
    if (!value[name].isBool()) Fail(path+"."+name,"expected bool");
    return value[name].asBool();
}
std::vector<float> Numbers(const Json::Value& value, std::size_t count, const std::string& path) {
    if (!value.isArray() || value.size()!=count) Fail(path,"unexpected numeric array length");
    std::vector<float> result;
    result.reserve(count);
    for (Json::ArrayIndex index=0;index<value.size();++index)
        result.push_back(Number(value[index],path+"["+std::to_string(index)+"]"));
    return result;
}
GameSproutRuntime::Vector2 Vector2Member(const Json::Value& value, const char* name,
                                       GameSproutRuntime::Vector2 fallback, const std::string& path) {
    if (!value.isMember(name)) return fallback;
    const auto data=Numbers(value[name],2,path+"."+name);
    return {data[0],data[1]};
}
GameSproutRuntime::Vector3 Vector3Member(const Json::Value& value, const char* name,
                                       GameSproutRuntime::Vector3 fallback, const std::string& path) {
    if (!value.isMember(name)) return fallback;
    const auto data=Numbers(value[name],3,path+"."+name);
    return {data[0],data[1],data[2]};
}
bool Normalized(GameSproutRuntime::Vector2 value) {
    return value.X>=0 && value.X<=1 && value.Y>=0 && value.Y<=1;
}
GameSproutRuntime::ResourceReference Resource(const Json::Value& value, const std::string& path) {
    if (value.isNull()) return {};
    Members(value,path,{"Guid","LocalID"});
    if (!value["Guid"].isString()) Fail(path+".Guid","expected asset GUID");
    std::string guid=value["Guid"].asString();
    if (guid.size()!=32) Fail(path+".Guid","expected 32 hexadecimal digits");
    bool nonzero=false;
    for (char& digit:guid) {
        if (digit>='A' && digit<='F') digit=static_cast<char>(digit-'A'+'a');
        if (!((digit>='0' && digit<='9') || (digit>='a' && digit<='f'))) Fail(path+".Guid","invalid hexadecimal digit");
        nonzero=nonzero || digit!='0';
    }
    if (!nonzero) Fail(path+".Guid","zero GUID is not a resource reference; omit an unbound reference");
    const std::uint64_t localID=Unsigned(value["LocalID"],path+".LocalID");
    if (!localID) Fail(path+".LocalID","resource LocalID must be nonzero");
    return {guid,localID};
}
template<typename Interface>
Interface* Component(GameSproutRuntime::IGameObject* object, const std::string& path) {
    Interface* result=object->AddComponent<Interface>();
    if (!result) Fail(path,"Runtime could not create component");
    return result;
}

// object=nullptr 为纯校验；有对象时执行已经校验过的设置。所有可选字段均使用 API 默认值。
void ComponentData(const Json::Value& value, GameSproutRuntime::IGameObject* object, const std::string& path) {
    if (!value.isObject() || !value["Type"].isString()) Fail(path,"component requires Type string");
    const std::string type=value["Type"].asString();
    if (type=="GameSproutRuntime.Transform") {
        Members(value,path,{"Type","Position","Rotation","Scale"});
        const auto position=Vector3Member(value,"Position",{},path);
        const auto scale=Vector3Member(value,"Scale",{1,1,1},path);
        GameSproutRuntime::Quaternion rotation;
        if (value.isMember("Rotation")) {
            const auto data=Numbers(value["Rotation"],4,path+".Rotation");
            rotation={data[0],data[1],data[2],data[3]};
            GameSproutRuntime::Quaternion checked;
            // 校验不重写输入；Runtime统一归一化，已经保存的单位旋转保持原始float分量。
            if (!rotation.TryNormalize(checked))
                Fail(path+".Rotation","rotation must be nonzero");
        }
        if (object) {
            auto* component=Component<GameSproutRuntime::ITransformComponent>(object,path);
            component->SetLocalPosition(position); component->SetLocalScale(scale);
            if (!component->SetLocalRotation(rotation)) Fail(path,"Runtime rejected rotation");
        }
    } else if (type=="GameSproutRuntime.RectTransform") {
        Members(value,path,{"Type","AnchorMin","AnchorMax","Pivot","AnchoredPosition","SizeDelta"});
        const auto minimum=Vector2Member(value,"AnchorMin",{0.5f,0.5f},path);
        const auto maximum=Vector2Member(value,"AnchorMax",{0.5f,0.5f},path);
        const auto pivot=Vector2Member(value,"Pivot",{0.5f,0.5f},path);
        const auto position=Vector2Member(value,"AnchoredPosition",{},path);
        const auto size=Vector2Member(value,"SizeDelta",{100,100},path);
        if (!Normalized(minimum) || !Normalized(maximum) || minimum.X>maximum.X || minimum.Y>maximum.Y)
            Fail(path,"invalid anchor range");
        if (!Normalized(pivot)) Fail(path+".Pivot","expected normalized pivot");
        if (object) {
            auto* component=Component<GameSproutRuntime::IRectTransformComponent>(object,path);
            component->SetAnchors(minimum,maximum); component->SetPivot(pivot);
            component->SetAnchoredPosition(position); component->SetSizeDelta(size);
        }
    } else if (type=="GameSproutRuntime.Image") {
        Members(value,path,{"Type","Texture","Color","UVRect"});
        const auto texture=Resource(value["Texture"],path+".Texture");
        GameSproutRuntime::Color color;
        if (value.isMember("Color")) {
            const auto data=Numbers(value["Color"],4,path+".Color");
            color={data[0],data[1],data[2],data[3]};
            if (color.R<0 || color.G<0 || color.B<0 || color.A<0 || color.A>1) Fail(path+".Color","invalid color range");
        }
        GameSproutRuntime::Rect uv{{0,0},{1,1}};
        if (value.isMember("UVRect")) {
            Members(value["UVRect"],path+".UVRect",{"Position","Size"});
            uv.Position=Vector2Member(value["UVRect"],"Position",{},path+".UVRect");
            uv.Size=Vector2Member(value["UVRect"],"Size",{1,1},path+".UVRect");
            if (uv.Size.X<0 || uv.Size.Y<0) Fail(path+".UVRect.Size","negative UV size");
        }
        if (object) {
            auto* component=Component<GameSproutRuntime::IImageComponent>(object,path);
            component->SetTexture(texture); component->SetColor(color); component->SetUVRect(uv);
        }
    } else if (type=="GameSproutRuntime.Canvas") {
        Members(value,path,{"Type","RenderMode","ReferenceResolution","PixelsPerUnit"});
        GameSproutRuntime::CanvasRenderMode mode=GameSproutRuntime::CanvasRenderMode::ScreenSpace;
        if (value.isMember("RenderMode")) {
            if (!value["RenderMode"].isString()) Fail(path+".RenderMode","expected string");
            const std::string name=value["RenderMode"].asString();
            if (name=="WorldSpace") mode=GameSproutRuntime::CanvasRenderMode::WorldSpace;
            else if (name!="ScreenSpace") Fail(path+".RenderMode","unknown canvas mode");
        }
        const auto resolution=Vector2Member(value,"ReferenceResolution",{1920,1080},path);
        const float pixels=FloatMember(value,"PixelsPerUnit",100,path);
        if (resolution.X<=0 || resolution.Y<=0 || pixels<=0) Fail(path,"canvas dimensions must be positive");
        if (object) {
            auto* component=Component<GameSproutRuntime::ICanvasComponent>(object,path);
            component->SetRenderMode(mode);
            if (!component->SetReferenceResolution(resolution) || !component->SetPixelsPerUnit(pixels)) Fail(path,"Runtime rejected canvas settings");
        }
    } else if (type=="GameSproutRuntime.Mesh") {
        Members(value,path,{"Type","Mesh"});
        const auto mesh=Resource(value["Mesh"],path+".Mesh");
        if (object) Component<GameSproutRuntime::IMeshComponent>(object,path)->SetMesh(mesh);
    } else if (type=="GameSproutRuntime.MeshRenderer") {
        Members(value,path,{"Type","Materials","Visible"});
        const bool visible=BoolMember(value,"Visible",true,path);
        std::vector<GameSproutRuntime::ResourceReference> materials;
        if (value.isMember("Materials")) {
            const auto& array=value["Materials"];
            if (!array.isArray() || array.size()>MaximumMaterials) Fail(path+".Materials","expected array of at most 1024 slots");
            materials.reserve(array.size());
            for (Json::ArrayIndex i=0;i<array.size();++i) materials.push_back(Resource(array[i],path+".Materials["+std::to_string(i)+"]"));
        }
        if (object) {
            auto* component=Component<GameSproutRuntime::IMeshRendererComponent>(object,path);
            if (!component->SetMaterials(materials)) Fail(path,"Runtime rejected material slots");
            component->SetVisible(visible);
        }
    } else if (type=="GameSproutRuntime.Camera") {
        Members(value,path,{"Type","Settings"});
        GameSproutRuntime::CameraSettings settings;
        if (value.isMember("Settings")) {
            const auto& json=value["Settings"];
            const std::string settingsPath=path+".Settings";
            Members(json,settingsPath,{"Projection","VerticalFovRadians","OrthographicHeight","NearPlane","FarPlane","CullingMask"});
            if (json.isMember("Projection")) {
                if (!json["Projection"].isString()) Fail(settingsPath+".Projection","expected string");
                const std::string mode=json["Projection"].asString();
                if (mode=="Orthographic") settings.Projection=GameSproutRuntime::CameraProjection::Orthographic;
                else if (mode!="Perspective") Fail(settingsPath+".Projection","unknown projection");
            }
            settings.VerticalFovRadians=FloatMember(json,"VerticalFovRadians",settings.VerticalFovRadians,settingsPath);
            settings.OrthographicHeight=FloatMember(json,"OrthographicHeight",settings.OrthographicHeight,settingsPath);
            settings.NearPlane=FloatMember(json,"NearPlane",settings.NearPlane,settingsPath);
            settings.FarPlane=FloatMember(json,"FarPlane",settings.FarPlane,settingsPath);
            if (json.isMember("CullingMask")) {
                const auto mask=Unsigned(json["CullingMask"],settingsPath+".CullingMask");
                if (mask>std::numeric_limits<std::uint32_t>::max()) Fail(settingsPath+".CullingMask","mask exceeds 32 bits");
                settings.CullingMask=static_cast<std::uint32_t>(mask);
            }
        }
        if (settings.VerticalFovRadians<=0 || settings.VerticalFovRadians>=GameSproutRuntime::Pi ||
            settings.OrthographicHeight<=0 || settings.NearPlane<0 || settings.FarPlane<=settings.NearPlane ||
            (settings.Projection==GameSproutRuntime::CameraProjection::Perspective && settings.NearPlane==0))
            Fail(path+".Settings","invalid camera projection parameters");
        if (object && !Component<GameSproutRuntime::ICameraComponent>(object,path)->SetSettings(settings))
            Fail(path,"Runtime rejected camera settings");
    } else Fail(path+".Type","unknown component: "+type);
}

struct Node {
    std::uint64_t ID=0;
    std::uint64_t ParentID=0;
    std::string Name;
    bool Active=true;
    const Json::Value* Components=nullptr;
    bool Camera=false;
};
struct Plan {
    std::string Name;
    bool RootActive=true;
    bool RootMainCamera=false;
    const Json::Value* RootComponents=nullptr;
    std::uint64_t CameraID=0;
    std::uint64_t Next=1;
    std::vector<Node> Nodes;
    std::map<std::uint64_t,std::size_t> Indices;
};
Plan ValidatePlan(const Json::Value& document) {
    Members(document,"Scene",{"Version","Name","RootActive","RootComponents","RootMainCamera","MainCameraID","NextObjectID","Objects"});
    if (Unsigned(document["Version"],"Scene.Version")!=1) Fail("Scene.Version","only version 1 is supported");
    Plan plan;
    plan.Name=Name(document["Name"],"Scene.Name");
    plan.RootActive=BoolMember(document,"RootActive",true,"Scene");
    plan.RootMainCamera=BoolMember(document,"RootMainCamera",false,"Scene");
    bool rootCamera=false;
    if (document.isMember("RootComponents")) {
        plan.RootComponents=&document["RootComponents"];
        if (!plan.RootComponents->isArray() || plan.RootComponents->size()>8) Fail("Scene.RootComponents","expected at most 8 components");
        std::set<std::string> types;
        for (Json::ArrayIndex c=0;c<plan.RootComponents->size();++c) {
            const auto& component=(*plan.RootComponents)[c];
            const std::string path="Scene.RootComponents["+std::to_string(c)+"]";
            ComponentData(component,nullptr,path);
            const std::string type=component["Type"].asString();
            if (!types.insert(type).second) Fail(path,"duplicate component type");
            rootCamera=rootCamera || type=="GameSproutRuntime.Camera";
            if (type=="GameSproutRuntime.Transform") {
                const auto p=Vector3Member(component,"Position",{},path);
                const auto s=Vector3Member(component,"Scale",{1,1,1},path);
                GameSproutRuntime::Quaternion r;
                if (component.isMember("Rotation")) {
                    const auto values=Numbers(component["Rotation"],4,path+".Rotation");
                    GameSproutRuntime::Quaternion(values[0],values[1],values[2],values[3]).TryNormalize(r);
                }
                if (p.X || p.Y || p.Z || s.X!=1 || s.Y!=1 || s.Z!=1 || r.X || r.Y || r.Z)
                    Fail(path,"scene root Transform must be identity");
            }
        }
    }
    if (document.isMember("MainCameraID")) plan.CameraID=Unsigned(document["MainCameraID"],"Scene.MainCameraID");
    const auto& objects=document["Objects"];
    if (!objects.isArray() || objects.size()>MaximumObjects) Fail("Scene.Objects","expected array of at most 100000 objects");
    plan.Nodes.reserve(objects.size());
    for (Json::ArrayIndex i=0;i<objects.size();++i) {
        const auto& object=objects[i];
        const std::string path="Objects["+std::to_string(i)+"]";
        Members(object,path,{"ID","ParentID","Name","Active","Components"});
        Node node;
        node.ID=Unsigned(object["ID"],path+".ID");
        if (!node.ID || !plan.Indices.emplace(node.ID,plan.Nodes.size()).second) Fail(path+".ID","zero or duplicate object ID");
        if (object.isMember("ParentID")) node.ParentID=Unsigned(object["ParentID"],path+".ParentID");
        node.Name=Name(object["Name"],path+".Name");
        node.Active=BoolMember(object,"Active",true,path);
        node.Components=&object["Components"];
        if (!node.Components->isArray() || node.Components->size()>8) Fail(path+".Components","expected at most 8 components");
        std::set<std::string> types;
        for (Json::ArrayIndex c=0;c<node.Components->size();++c) {
            const auto& component=(*node.Components)[c];
            const std::string componentPath=path+".Components["+std::to_string(c)+"]";
            ComponentData(component,nullptr,componentPath);
            const std::string type=component["Type"].asString();
            if (!types.insert(type).second) Fail(componentPath+".Type","duplicate component type");
            node.Camera=node.Camera || type=="GameSproutRuntime.Camera";
        }
        plan.Nodes.push_back(std::move(node));
    }
    for (const auto& node:plan.Nodes)
        if (node.ParentID && !plan.Indices.count(node.ParentID)) Fail("Scene.Objects","missing parent for ID "+std::to_string(node.ID));
    std::uint64_t maximum=0;
    for (const auto& node:plan.Nodes) maximum=std::max(maximum,node.ID);
    plan.Next=maximum==std::numeric_limits<std::uint64_t>::max()?0:maximum+1;
    if (document.isMember("NextObjectID")) {
        plan.Next=Unsigned(document["NextObjectID"],"Scene.NextObjectID");
        if (plan.Next && plan.Next<=maximum) Fail("Scene.NextObjectID","must exceed all existing file IDs");
    }
    // 迭代染色与缓存深度，避免恶意深层树造成递归栈溢出或二次复杂度。
    std::vector<unsigned char> state(plan.Nodes.size(),0);
    std::vector<std::size_t> depths(plan.Nodes.size(),0), chain;
    for (std::size_t start=0;start<plan.Nodes.size();++start) {
        if (state[start]==2) continue;
        chain.clear();
        std::size_t current=start;
        while (state[current]!=2) {
            if (state[current]==1) Fail("Scene.Objects","parent cycle detected");
            state[current]=1; chain.push_back(current);
            if (chain.size()>MaximumDepth) Fail("Scene.Objects","hierarchy depth exceeds 1024");
            const auto parent=plan.Nodes[current].ParentID;
            if (!parent) break;
            current=plan.Indices.at(parent);
        }
        std::size_t depth=state[current]==2?depths[current]:0;
        for (auto it=chain.rbegin();it!=chain.rend();++it) {
            depths[*it]=++depth; state[*it]=2;
            if (depth>MaximumDepth) Fail("Scene.Objects","hierarchy depth exceeds 1024");
        }
    }
    if (plan.CameraID) {
        const auto camera=plan.Indices.find(plan.CameraID);
        if (camera==plan.Indices.end() || !plan.Nodes[camera->second].Camera)
            Fail("Scene.MainCameraID","main camera must reference an object with Camera component");
    }
    if (plan.RootMainCamera && (plan.CameraID || !rootCamera))
        Fail("Scene.RootMainCamera","requires root Camera and MainCameraID=0");
    return plan;
}
Json::Value Array(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);
    for (float value:values) result.append(value);
    return result;
}
}

namespace SceneDocument {
Json::Value CreateDefault(const std::string& name) {
    Json::Value result(Json::objectValue);
    result["Version"]=Json::UInt(1); result["Name"]=name; result["MainCameraID"]=Json::UInt64(1);
    Json::Value camera(Json::objectValue);
    camera["ID"]=Json::UInt64(1); camera["ParentID"]=Json::UInt64(0);
    camera["Name"]="Main Camera"; camera["Active"]=true;
    camera["Components"]=Json::Value(Json::arrayValue);
    Json::Value transform(Json::objectValue);
    transform["Type"]="GameSproutRuntime.Transform";
    transform["Position"]=Array({0,0,3}); transform["Rotation"]=Array({0,0,0,1}); transform["Scale"]=Array({1,1,1});
    camera["Components"].append(transform);
    Json::Value component(Json::objectValue);
    component["Type"]="GameSproutRuntime.Camera";
    const GameSproutRuntime::CameraSettings settings;
    auto& json=component["Settings"];
    json["Projection"]="Perspective"; json["VerticalFovRadians"]=settings.VerticalFovRadians;
    json["OrthographicHeight"]=settings.OrthographicHeight;
    json["NearPlane"]=settings.NearPlane; json["FarPlane"]=settings.FarPlane;
    json["CullingMask"]=Json::UInt(settings.CullingMask);
    camera["Components"].append(component);
    result["Objects"]=Json::Value(Json::arrayValue); result["Objects"].append(camera);
    return result;
}

bool Instantiate(const Json::Value& document, GameSproutRuntime::IGameSproutRuntime& runtime,
                 GameSproutRuntime::IScene*& output, std::string& error) {
    ObjectIDs ids;
    return Instantiate(document,runtime,output,ids,error);
}
bool Validate(const Json::Value& document,std::string& error) {
    error.clear();
    try { ValidatePlan(document);return true; }
    catch (const std::exception& exception) {error=exception.what();}
    catch (...) {error="Scene: unexpected validation failure";}
    return false;
}
bool Instantiate(const Json::Value& document, GameSproutRuntime::IGameSproutRuntime& runtime,
                 GameSproutRuntime::IScene*& output, ObjectIDs& ids, std::string& error) {
    GameSproutRuntime::IScene* candidate=nullptr;
    error.clear();
    if (runtime.IsExecuting()) {
        error="Scene: Runtime is executing an update, render, traversal or event callback; synchronous scene loading is not allowed";
        return false;
    }
    try {
        const Plan plan=ValidatePlan(document);
        candidate=runtime.CreateScene(plan.Name);
        if (!candidate) Fail("Scene","Runtime could not create scene");
        std::map<std::uint64_t,GameSproutRuntime::IGameObject*> objects;
        ObjectIDs candidateIDs;
        candidateIDs.Next=plan.Next;
        for (const auto& node:plan.Nodes) {
            auto* object=candidate->CreateGameObject(node.Name);
            if (!object) Fail("Object "+std::to_string(node.ID),"Runtime could not create object");
            objects.emplace(node.ID,object);
            candidateIDs.RuntimeToFile.emplace(object->GetInstanceID(),node.ID);
        }
        // 全部创建后按文件顺序重挂载，文件可先写子节点，仍保留每组兄弟节点的顺序。
        for (const auto& node:plan.Nodes) {
            if (node.ParentID && !objects.at(node.ParentID)->AddChild(objects.at(node.ID),false))
                Fail("Object "+std::to_string(node.ID),"Runtime rejected parent relationship");
        }
        for (const auto& node:plan.Nodes) {
            auto* object=objects.at(node.ID);
            for (Json::ArrayIndex c=0;c<node.Components->size();++c)
                ComponentData((*node.Components)[c],object,"Object "+std::to_string(node.ID)+".Components["+std::to_string(c)+"]");
            object->SetActive(node.Active);
        }
        if (plan.RootComponents) for (Json::ArrayIndex c=0;c<plan.RootComponents->size();++c)
            ComponentData((*plan.RootComponents)[c],candidate,"Scene.RootComponents["+std::to_string(c)+"]");
        candidate->SetActive(plan.RootActive);
        for (const auto& node:plan.Nodes) {
            const auto world=objects.at(node.ID)->GetTransform()->GetWorldMatrix();
            for (float value:world.Data) if (!std::isfinite(value)) Fail("Object "+std::to_string(node.ID),"world transform overflows");
        }
        auto* camera=plan.RootMainCamera?candidate->GetComponent<GameSproutRuntime::ICameraComponent>():
            plan.CameraID?objects.at(plan.CameraID)->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr;
        if (!candidate->SetMainCamera(camera)) Fail("Scene.MainCameraID","Runtime rejected main camera");
        ids.RuntimeToFile.swap(candidateIDs.RuntimeToFile);ids.Next=candidateIDs.Next;
        output=candidate;
        return true;
    } catch (const std::exception& exception) {
        error=exception.what();
    } catch (...) {
        error="Scene: unexpected instantiation failure";
    }
    if (candidate) {
        try {
            if (!runtime.DestroyScene(candidate)) error+="; candidate scene rollback was rejected";
        } catch (...) { error+="; candidate scene rollback failed"; }
    }
    return false;
}

bool RestoreSubtree(const Json::Value& document,GameSproutRuntime::IGameSproutRuntime& runtime,
                    GameSproutRuntime::IScene& scene,GameSproutRuntime::IGameObject& parent,
                    std::size_t siblingIndex,ObjectIDs& ids,GameSproutRuntime::IGameObject*& output,std::string& error) {
    error.clear();
    if(runtime.IsExecuting()){error="Cannot restore a subtree inside a Runtime callback";return false;}
    if(runtime.QueryGameObject(scene.GetInstanceID())!=&scene || runtime.QueryGameObject(parent.GetInstanceID())!=&parent || parent.GetScene()!=&scene) {
        error="Subtree parent must be a live object in the target scene";return false;
    }
    std::vector<std::uint64_t> created;
    const auto previousCamera=scene.GetMainCamera()?scene.GetMainCamera()->GetGameObject()->GetInstanceID():0;
    try {
        const Plan plan=ValidatePlan(document);
        if(plan.Nodes.empty() || plan.RootMainCamera || (plan.RootComponents&&!plan.RootComponents->empty()))
            Fail("Subtree","expected nodes without scene-root components");
        const Node* root=nullptr;
        for(const auto& node:plan.Nodes) if(!node.ParentID) {
            if(root) Fail("Subtree","expected exactly one root");root=&node;
        }
        if(!root) Fail("Subtree","missing root");
        ObjectIDs candidateIDs=ids;Json::Value combined;
        if(!Serialize(scene,candidateIDs,combined,error))return false;
        const auto parentID=&parent==&scene?0:candidateIDs.RuntimeToFile.at(parent.GetInstanceID());
        for(const auto& data:document["Objects"]) {
            Json::Value node=data;
            if(node["ID"].asUInt64()==root->ID)node["ParentID"]=Json::UInt64(parentID);
            combined["Objects"].append(std::move(node));
        }
        if(plan.CameraID) {combined["MainCameraID"]=Json::UInt64(plan.CameraID);combined["RootMainCamera"]=false;}
        // 总节点数、真实父链深度、文件ID冲突等均在创建任何对象之前检查。
        ValidatePlan(combined);
        std::map<std::uint64_t,GameSproutRuntime::IGameObject*> objects;
        created.reserve(plan.Nodes.size());
        for(const auto& node:plan.Nodes) {
            auto* object=scene.CreateGameObject(node.Name);
            if(!object)Fail("Subtree","Runtime could not create object");
            created.push_back(object->GetInstanceID());
            objects.emplace(node.ID,object);
            candidateIDs.RuntimeToFile.emplace(object->GetInstanceID(),node.ID);
        }
        for(const auto& node:plan.Nodes) {
            auto* owner=node.ParentID?objects.at(node.ParentID):&parent;
            if(!owner->AddChild(objects.at(node.ID),false))Fail("Subtree","Runtime rejected restored parent");
        }
        for(const auto& node:plan.Nodes) {
            auto* object=objects.at(node.ID);
            for(Json::ArrayIndex index=0;index<node.Components->size();++index)
                ComponentData((*node.Components)[index],object,"Subtree.Components");
            object->SetActive(node.Active);
        }
        auto* restored=objects.at(root->ID);
        if(!restored->SetSiblingIndex(siblingIndex))Fail("Subtree","Runtime rejected restored sibling order");
        if(plan.CameraID && !scene.SetMainCamera(objects.at(plan.CameraID)->GetComponent<GameSproutRuntime::ICameraComponent>()))
            Fail("Subtree","Runtime rejected restored main camera");
        Json::Value verified;
        if(!Serialize(scene,candidateIDs,verified,error))throw std::runtime_error(error);
        ids.RuntimeToFile.swap(candidateIDs.RuntimeToFile);ids.Next=candidateIDs.Next;output=restored;return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Unexpected subtree restoration failure";}
    for(const auto id:created) if(auto* object=runtime.QueryGameObject(id)) {
        try {if(!scene.DestroyGameObject(object))error+="; subtree rollback rejected";}
        catch(...) {error+="; subtree rollback failed";}
    }
    auto* previous=previousCamera?runtime.QueryGameObject(previousCamera):nullptr;
    scene.SetMainCamera(previous?previous->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr);
    return false;
}
}

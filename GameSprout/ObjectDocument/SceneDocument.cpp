#include "SceneDocument.h"
#include "SceneAnimationDocument.h"
#include "SceneScriptDocument.h"
#include "ScenePhysicsDocument.h"
#include "SceneLightingDocument.h"
#include "SceneEnvironmentLightingDocument.h"
#include "SceneRendererShadows.h"
#include "PrefabDocument.h"
#include "RuntimeUIApi.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
constexpr std::size_t MaximumObjects = 100000;
constexpr std::size_t MaximumDepth = 1024;
constexpr std::size_t MaximumMaterials = 1024;
constexpr std::size_t MaximumComponents = 1024;
constexpr std::size_t MaximumNameBytes = 1024;

[[noreturn]] void Fail(const std::string& path, const std::string& reason) {
    throw std::runtime_error(path + ": " + reason);
}
// Roll back candidate objects before releasing the assembly barrier. Releasing
// it first could initialize a scene whose properties/references are incomplete.
void FinishConstructionRollback(GameSproutRuntime::IGameSproutRuntime& runtime,
                                bool& constructionOpen,std::string& error) {
    if(!constructionOpen)return;
    constructionOpen=false;
    try {
        std::string cleanup;
        if(!runtime.EndConstruction(cleanup)) {
            error+="; construction rollback could not finish";
            if(!cleanup.empty())error+=": "+cleanup;
        }
    } catch(const std::exception& exception) {error+="; construction rollback failed: "+std::string(exception.what());}
    catch(...) {error+="; construction rollback failed";}
}
void Members(const Json::Value& value, const std::string& path, std::initializer_list<const char*> allowed) {
    if (!value.isObject()) Fail(path,"expected object");
    const auto last=value.end();
    for(auto member=value.begin();member!=last;++member) {
        const char* end=nullptr;
        const char* begin=member.memberName(&end);
        const std::string_view key(begin,static_cast<std::size_t>(end-begin));
        if (std::none_of(allowed.begin(),allowed.end(),[&](const char* name){return key==name;}))
            Fail(path + "." + std::string(key),"unknown field");
    }
}
std::uint64_t Unsigned(const Json::Value& value, const std::string& path) {
    if ((value.type()!=Json::intValue && value.type()!=Json::uintValue) || !value.isUInt64())
        Fail(path,"expected unsigned integer");
    return value.asUInt64();
}
GameSproutRuntime::StaticFlags StaticFlagsValue(const Json::Value& value,const std::string& path) {
    const auto raw=Unsigned(value,path);
    if(raw>std::numeric_limits<std::uint32_t>::max())Fail(path,"static flags exceed 32 bits");
    const auto flags=static_cast<GameSproutRuntime::StaticFlags>(static_cast<std::uint32_t>(raw));
    if(!GameSproutRuntime::IsValidStaticFlags(flags))Fail(path,"unknown static flag bits");
    return flags;
}
std::string Name(const Json::Value& value, const std::string& path) {
    if (!value.isString()) Fail(path,"expected string");
    const std::string result=value.asString();
    if (result.empty() || result.size()>MaximumNameBytes || result.find('\0')!=std::string::npos)
        Fail(path,"name must contain 1..1024 bytes without NUL");
    return result;
}
std::string_view ComponentType(const Json::Value& component) {
    const char* begin=nullptr;
    const char* end=nullptr;
    if(!component.isObject()||!component["Type"].getString(&begin,&end))return {};
    return {begin,static_cast<std::size_t>(end-begin)};
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
template<std::size_t Count>
std::array<float,Count> Numbers(const Json::Value& value, const std::string& path) {
    if (!value.isArray() || value.size()!=Count) Fail(path,"unexpected numeric array length");
    std::array<float,Count> result;
    for (Json::ArrayIndex index=0;index<value.size();++index)
        result[index]=Number(value[index],path+"["+std::to_string(index)+"]");
    return result;
}
GameSproutRuntime::Vector2 Vector2Member(const Json::Value& value, const char* name,
                                       GameSproutRuntime::Vector2 fallback, const std::string& path) {
    if (!value.isMember(name)) return fallback;
    const auto data=Numbers<2>(value[name],path+"."+name);
    return {data[0],data[1]};
}
GameSproutRuntime::Vector3 Vector3Member(const Json::Value& value, const char* name,
                                       GameSproutRuntime::Vector3 fallback, const std::string& path) {
    if (!value.isMember(name)) return fallback;
    const auto data=Numbers<3>(value[name],path+"."+name);
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
std::string_view ComponentData(const Json::Value& value, GameSproutRuntime::IGameObject* object, const std::string& path,
                              GameSproutRuntime::IGameSproutRuntime* runtime=nullptr) {
    if(value.isObject()&&value.isMember("ID")) {
        if(!Unsigned(value["ID"],path+".ID"))Fail(path+".ID","component ID must be nonzero");
        // ID is document metadata, already validated here. Borrow the original
        // component instead of recursively deep-copying Settings/Shape/Fields
        // merely to remove that key during both validation and assembly.
    }
    const auto type=ComponentType(value);
    if(type.empty()&&(!value.isObject()||!value["Type"].isString()))
        Fail(path,"component requires Type string");
    if (type=="GameSproutRuntime.Transform") {
        Members(value,path,{"ID","Type","Position","Rotation","Scale"});
        const auto position=Vector3Member(value,"Position",{},path);
        const auto scale=Vector3Member(value,"Scale",{1,1,1},path);
        GameSproutRuntime::Quaternion rotation;
        if (value.isMember("Rotation")) {
            const auto data=Numbers<4>(value["Rotation"],path+".Rotation");
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
        Members(value,path,{"ID","Type","AnchorMin","AnchorMax","Pivot","AnchoredPosition","SizeDelta"});
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
        Members(value,path,{"ID","Type","Texture","Color","UVRect"});
        const auto texture=Resource(value["Texture"],path+".Texture");
        GameSproutRuntime::Color color;
        if (value.isMember("Color")) {
            const auto data=Numbers<4>(value["Color"],path+".Color");
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
        Members(value,path,{"ID","Type","RenderMode","ReferenceResolution","PixelsPerUnit"});
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
    } else if (type=="GameSproutRuntime.Text") {
        Members(value,path,{"ID","Type","Text","Font","Color","FontSize"});
        std::string text;
        if (value.isMember("Text")) {
            if (!value["Text"].isString()) Fail(path+".Text","expected string");
            text=value["Text"].asString();
            if (!GameSproutRuntime::IsValidUIText(text)) Fail(path+".Text","expected valid UTF-8 text within the UI text size limit");
        }
        const auto font=Resource(value["Font"],path+".Font");
        GameSproutRuntime::Color color;
        if (value.isMember("Color")) {
            const auto data=Numbers<4>(value["Color"],path+".Color");
            color={data[0],data[1],data[2],data[3]};
            if (color.R<0 || color.G<0 || color.B<0 || color.A<0 || color.A>1) Fail(path+".Color","invalid color range");
        }
        const auto size=FloatMember(value,"FontSize",GameSproutRuntime::DefaultUIFontSize,path);
        if (!GameSproutRuntime::IsValidUIFontSize(size)) Fail(path+".FontSize","font size is outside the supported UI range");
        if (object) {
            auto* component=Component<GameSproutRuntime::ITextComponent>(object,path);
            if (!component->SetText(text) || !component->SetFontSize(size)) Fail(path,"Runtime rejected text settings");
            component->SetFont(font);
            component->SetColor(color);
        }
    } else if (type=="GameSproutRuntime.Button") {
        Members(value,path,{"ID","Type","Interactable"});
        const bool interactable=BoolMember(value,"Interactable",true,path);
        if (object) Component<GameSproutRuntime::IButtonComponent>(object,path)->SetInteractable(interactable);
    } else if (type=="GameSproutRuntime.Mesh") {
        Members(value,path,{"ID","Type","Mesh"});
        const auto mesh=Resource(value["Mesh"],path+".Mesh");
        if (object) Component<GameSproutRuntime::IMeshComponent>(object,path)->SetMesh(mesh);
    } else if (type=="GameSproutRuntime.MeshRenderer") {
        Members(value,path,{"ID","Type","Materials","Visible","ShadowCastingMode","ReceiveShadows"});
        const bool visible=BoolMember(value,"Visible",true,path);
        const auto shadows=SceneRendererShadows::Read(value,path);
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
            if (!component->SetShadowCastingMode(shadows.Casting)) Fail(path+".ShadowCastingMode","Runtime rejected shadow mode");
            component->SetReceiveShadows(shadows.Receive);
        }
    } else if (SceneLightingDocument::Handles(type)) {
        SceneLightingDocument::Validate(value,path);
        if(object)SceneLightingDocument::Create(value,*object);
    } else if (ScenePhysicsDocument::Handles(type)) {
        if(object)ScenePhysicsDocument::Create(value,*object);
        else ScenePhysicsDocument::Validate(value,path);
    } else if (SceneAnimationDocument::Handles(type)) {
        SceneAnimationDocument::Validate(value,path);
        if (object) SceneAnimationDocument::Create(value,*object);
    } else if (type=="GameSproutRuntime.Camera") {
        Members(value,path,{"ID","Type","Settings"});
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
    } else if(type=="GameSproutRuntime.ScriptComponent") {
        SceneScriptDocument::Validate(value,path);
        if(object) {
            if(!runtime)Fail(path,"script restoration requires Runtime");
            SceneScriptDocument::Create(value,*object,*runtime);
        }
    } else Fail(path+".Type","unknown component: "+std::string(type));
    return type;
}

struct Node {
    std::uint64_t ID=0;
    std::uint64_t ParentID=0;
    std::string Name;
    bool Active=true;
    std::uint8_t Layer=0;
    std::uint64_t TagID=0;
    GameSproutRuntime::StaticFlags Static=GameSproutRuntime::StaticFlags::None;
    const Json::Value* Components=nullptr;
    bool Camera=false;
};
struct Plan {
    GameSproutRuntime::EnvironmentLightingSettings EnvironmentLighting;
    std::string Name;
    bool RootActive=true;
    std::uint8_t RootLayer=0;
    std::uint64_t RootTagID=0;
    GameSproutRuntime::StaticFlags RootStatic=GameSproutRuntime::StaticFlags::None;
    bool RootMainCamera=false;
    const Json::Value* RootComponents=nullptr;
    std::uint64_t CameraID=0;
    std::uint64_t Next=1;
    std::size_t Depth=0;
    std::vector<Node> Nodes;
    std::map<std::uint64_t,std::size_t> Indices;
};
Node ReadNode(const Json::Value& object,const std::string& path,std::vector<std::string_view>& types) {
    Members(object,path,{"ID","ParentID","Name","Active","Layer","TagID","StaticFlags","Components"});
    Node node;
    node.ID=Unsigned(object["ID"],path+".ID");
    if(!node.ID)Fail(path+".ID","zero or duplicate object ID");
    if(object.isMember("ParentID"))node.ParentID=Unsigned(object["ParentID"],path+".ParentID");
    node.Name=Name(object["Name"],path+".Name");
    node.Active=BoolMember(object,"Active",true,path);
    if(object.isMember("Layer")) {
        const auto layer=Unsigned(object["Layer"],path+".Layer");
        if(layer>31)Fail(path+".Layer","expected 0..31");
        node.Layer=static_cast<std::uint8_t>(layer);
    }
    if(object.isMember("TagID"))node.TagID=Unsigned(object["TagID"],path+".TagID");
    if(object.isMember("StaticFlags"))node.Static=StaticFlagsValue(object["StaticFlags"],path+".StaticFlags");
    node.Components=&object["Components"];
    if(!node.Components->isArray()||node.Components->size()>MaximumComponents)
        Fail(path+".Components","expected at most 1024 components");
    types.clear();
    std::set<std::uint64_t> scriptInstances;
    std::set<std::string> scriptTypes;
    for(Json::ArrayIndex index=0;index<node.Components->size();++index) {
        const auto& component=(*node.Components)[index];
        const std::string componentPath=path+".Components["+std::to_string(index)+"]";
        const auto type=ComponentData(component,nullptr,componentPath);
        if(type=="GameSproutRuntime.ScriptComponent") {
            if(!scriptInstances.insert(component["InstanceID"].asUInt64()).second)
                Fail(componentPath+".Type","duplicate component identity");
            if(!scriptTypes.insert(component["ScriptTypeID"].asString()).second)
                Fail(componentPath+".ScriptTypeID","only one component of the same script type is allowed on an object");
        } else if(!ScenePhysicsDocument::IsCollider(type)) {
            if(std::find(types.begin(),types.end(),type)!=types.end())
                Fail(componentPath+".Type","duplicate component identity");
            types.push_back(type);
        }
        // Colliders may repeat. Explicit IDs are checked separately against
        // the other authored object and component IDs.
        node.Camera=node.Camera||type=="GameSproutRuntime.Camera";
    }
    return node;
}
void ValidateComponentIDs(const Json::Value& components,std::unordered_set<std::uint64_t>& stableIDs,std::uint64_t& maximum) {
    for(const auto& component:components)if(component.isMember("ID")) {
        const auto id=Unsigned(component["ID"],"Component.ID");
        if(!id||!stableIDs.insert(id).second)Fail("Component.ID","zero or duplicate stable ID");
        maximum=std::max(maximum,id);
    }
}
// Project definitions are installed by the host before opening any document.
// Reject undefined identities before creating nodes or running component hooks.
void ValidateProjectTags(const Plan& plan,const GameSproutRuntime::IGameSproutRuntime& runtime) {
    const auto& tags=runtime.GetProjectSettings().Tags;
    const auto defined=[&](std::uint64_t id){return std::any_of(tags.begin(),tags.end(),[&](const auto& tag){return tag.ID==id;});};
    if(!defined(plan.RootTagID))Fail("Scene.RootTagID","undefined project Tag identity");
    for(const auto& node:plan.Nodes)if(!defined(node.TagID))
        Fail("Object "+std::to_string(node.ID)+".TagID","undefined project Tag identity");
}
void ComponentIdentities(const Json::Value* data,GameSproutRuntime::IGameObject& object,SceneDocument::ObjectIDs& ids,
    std::vector<GameSproutRuntime::SceneComponentBinding>* bindings=nullptr) {
    std::bitset<MaximumComponents> consumed;
    const auto entryCount=data?data->size():0;
    const auto componentCount=object.GetComponentCount();
    Json::ArrayIndex firstUnmatched=0;
    for(std::size_t index=0;index<componentCount;++index) {
        auto* component=object.GetComponentAt(index);
        if(!component)Fail("Component.ID","invalid Runtime component");
        const auto* typeName=component->GetTypeName();
        if(!typeName)Fail("Component.ID","invalid Runtime component type");
        const std::string_view type=typeName;
        const auto* script=type=="GameSproutRuntime.ScriptComponent"?
            dynamic_cast<GameSproutRuntime::IScriptComponent*>(component):nullptr;
        const auto scriptInstance=script?script->GetScriptInstanceID():0;
        // Saved scenes write components in Runtime slot order. Keep matching
        // by identity when order differs, including repeatable colliders.
        std::uint64_t id=0;
        for(Json::ArrayIndex entry=firstUnmatched;entry<entryCount;++entry) {
            if(consumed[entry])continue;
            const auto& fields=(*data)[entry];
            if(ComponentType(fields)!=type)continue;
            if(script&&fields["InstanceID"].asUInt64()!=scriptInstance)continue;
            id=fields["ID"].asUInt64();
            consumed.set(entry);
            while(firstUnmatched<entryCount&&consumed[firstUnmatched])++firstUnmatched;
            break;
        }
        if(!id) {
            if(!ids.Next)Fail("Component.ID","stable ID space exhausted");
            id=ids.Next;ids.Next=id==std::numeric_limits<std::uint64_t>::max()?0:id+1;
        }
        ids.BindComponentFileID(component,id);
        if(bindings)bindings->push_back({object.GetInstanceID(),component->GetComponentInstanceID(),id});
    }
}
// Publish once after every component exists, before any native script callback.
// Existing scene bindings are retained when a Prefab adds another subtree.
void PublishComponentIdentities(GameSproutRuntime::IScene& scene,const SceneDocument::ObjectIDs& ids,
    GameSproutRuntime::IGameSproutRuntime& runtime) {
    std::vector<GameSproutRuntime::SceneComponentIdentity> identities;
    std::vector<GameSproutRuntime::IGameObject*> pending{&scene};
    while(!pending.empty()) {
        auto* object=pending.back();pending.pop_back();
        for(std::size_t index=0;index<object->GetComponentCount();++index) {
            auto* component=object->GetComponentAt(index);
            auto fileID=ids.FindComponentFileID(component);
            if(!fileID)runtime.GetSceneComponentFileID(component,fileID);
            if(fileID)identities.push_back({component,fileID});
        }
        for(std::size_t index=0;index<object->GetChildCount();++index)pending.push_back(object->GetChild(index));
    }
    std::string error;
    if(!runtime.SetSceneComponentIdentities(&scene,identities,error))Fail("Component.Identity",error);
}
Plan ValidatePlan(const Json::Value& document, bool checkReferences=true) {
    Members(document,"Scene",{"Version","Name","RootActive","RootLayer","RootTagID","RootStaticFlags","RootComponents","RootMainCamera","MainCameraID","NextObjectID","Objects","Annotations","EnvironmentLighting"});
    if(document.isMember("Annotations")&&!document["Annotations"].isObject())Fail("Scene.Annotations","expected object");
    if (Unsigned(document["Version"],"Scene.Version")!=GameSproutRuntime::SceneDocumentVersion)
        Fail("Scene.Version","expected current Unity-coordinate scene version 2; recreate or re-export the source document");
    Plan plan;
    if(document.isMember("EnvironmentLighting"))plan.EnvironmentLighting=SceneEnvironmentLightingDocument::Read(document["EnvironmentLighting"]);
    plan.Name=Name(document["Name"],"Scene.Name");
    plan.RootActive=BoolMember(document,"RootActive",true,"Scene");
    if(document.isMember("RootLayer")) {
        const auto layer=Unsigned(document["RootLayer"],"Scene.RootLayer");
        if(layer>31)Fail("Scene.RootLayer","expected 0..31");
        plan.RootLayer=static_cast<std::uint8_t>(layer);
    }
    if(document.isMember("RootTagID"))plan.RootTagID=Unsigned(document["RootTagID"],"Scene.RootTagID");
    if(document.isMember("RootStaticFlags"))plan.RootStatic=StaticFlagsValue(document["RootStaticFlags"],"Scene.RootStaticFlags");
    plan.RootMainCamera=BoolMember(document,"RootMainCamera",false,"Scene");
    bool rootCamera=false;
    if (document.isMember("RootComponents")) {
        plan.RootComponents=&document["RootComponents"];
        if (!plan.RootComponents->isArray() || plan.RootComponents->size()>16) Fail("Scene.RootComponents","expected at most 16 components");
        std::set<std::string_view> types;
        for (Json::ArrayIndex c=0;c<plan.RootComponents->size();++c) {
            const auto& component=(*plan.RootComponents)[c];
            const std::string path="Scene.RootComponents["+std::to_string(c)+"]";
            const auto type=ComponentData(component,nullptr,path);
            if(SceneAnimationDocument::Handles(type)||ScenePhysicsDocument::Handles(type)||SceneLightingDocument::Handles(type)||type=="GameSproutRuntime.ScriptComponent")Fail(path,"animation/physics/light/script components require a regular object, not the scene root");
            if (!types.insert(type).second) Fail(path,"duplicate component identity");
            rootCamera=rootCamera || type=="GameSproutRuntime.Camera";
            if (type=="GameSproutRuntime.Transform") {
                const auto p=Vector3Member(component,"Position",{},path);
                const auto s=Vector3Member(component,"Scale",{1,1,1},path);
                GameSproutRuntime::Quaternion r;
                if (component.isMember("Rotation")) {
                    const auto values=Numbers<4>(component["Rotation"],path+".Rotation");
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
    std::vector<std::string_view> types;
    for (Json::ArrayIndex i=0;i<objects.size();++i) {
        const std::string path="Objects["+std::to_string(i)+"]";
        auto node=ReadNode(objects[i],path,types);
        if(!plan.Indices.emplace(node.ID,plan.Nodes.size()).second)Fail(path+".ID","zero or duplicate object ID");
        plan.Nodes.push_back(std::move(node));
    }
    for (const auto& node:plan.Nodes)
        if (node.ParentID && !plan.Indices.count(node.ParentID)) Fail("Scene.Objects","missing parent for ID "+std::to_string(node.ID));
    std::uint64_t maximum=0;
    std::unordered_set<std::uint64_t> stableIDs;
    stableIDs.reserve(plan.Nodes.size());
    for (const auto& node:plan.Nodes) {maximum=std::max(maximum,node.ID);stableIDs.insert(node.ID);}
    if(plan.RootComponents)ValidateComponentIDs(*plan.RootComponents,stableIDs,maximum);
    for(const auto& node:plan.Nodes)ValidateComponentIDs(*node.Components,stableIDs,maximum);
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
            plan.Depth=std::max(plan.Depth,depth);
        }
    }
    if (plan.CameraID) {
        const auto camera=plan.Indices.find(plan.CameraID);
        if (camera==plan.Indices.end() || !plan.Nodes[camera->second].Camera)
            Fail("Scene.MainCameraID","main camera must reference an object with Camera component");
    }
    if (plan.RootMainCamera && (plan.CameraID || !rootCamera))
        Fail("Scene.RootMainCamera","requires root Camera and MainCameraID=0");
    if(checkReferences)SceneAnimationDocument::ValidateReferences(document);
    return plan;
}
Json::Value Array(std::initializer_list<float> values) {
    Json::Value result(Json::arrayValue);
    for (float value:values) result.append(value);
    return result;
}

struct SceneAssemblyPlan {
    struct AnimationBinding {
        std::uint64_t Owner;
        const Json::Value* Component;
    };
    Plan Values;
    std::vector<const Node*> ParentFirst;
    std::vector<AnimationBinding> Animation;
    std::vector<const Node*> TagOwners;

    explicit SceneAssemblyPlan(const Json::Value& document):Values(ValidatePlan(document)) {
        // Retain source sibling order even when a child precedes its parent.
        std::map<std::uint64_t,std::vector<const Node*>> children;
        std::vector<AnimationBinding> skins;
        std::unordered_set<std::uint64_t> tags{Values.RootTagID};
        for(const auto& node:Values.Nodes) {
            children[node.ParentID].push_back(&node);
            if(tags.insert(node.TagID).second)TagOwners.push_back(&node);
            for(const auto& component:*node.Components) {
                const auto type=ComponentType(component);
                if(type=="GameSproutRuntime.Animator")Animation.push_back({node.ID,&component});
                else if(type=="GameSproutRuntime.SkinnedMeshRenderer")skins.push_back({node.ID,&component});
            }
        }
        Animation.insert(Animation.end(),skins.begin(),skins.end());
        if(const auto roots=children.find(0);roots!=children.end())ParentFirst=std::move(roots->second);
        ParentFirst.reserve(Values.Nodes.size());
        for(std::size_t index=0;index<ParentFirst.size();++index) {
            const auto descendants=children.find(ParentFirst[index]->ID);
            if(descendants!=children.end())
                ParentFirst.insert(ParentFirst.end(),descendants->second.begin(),descendants->second.end());
        }
    }

    void ValidateTags(const GameSproutRuntime::RuntimeProjectSettings& settings) const {
        std::unordered_set<std::uint64_t> defined;
        defined.reserve(settings.Tags.size());
        for(const auto& tag:settings.Tags)defined.insert(tag.ID);
        if(!defined.contains(Values.RootTagID))Fail("Scene.RootTagID","undefined project Tag identity");
        for(const auto* node:TagOwners)if(!defined.contains(node->TagID))
            Fail("Object "+std::to_string(node->ID)+".TagID","undefined project Tag identity");
    }
};
}

namespace SceneDocument {
struct PreparedScene::Data {
    Json::Value Document;
    SceneAssemblyPlan Plan;

    explicit Data(Json::Value&& document):Document(std::move(document)),Plan(Document) {}
};

PreparedScene::~PreparedScene() {delete _Data;}
PreparedScene::PreparedScene(PreparedScene&& other) noexcept:_Data(std::exchange(other._Data,nullptr)) {}
PreparedScene& PreparedScene::operator=(PreparedScene&& other) noexcept {
    if(this!=&other) {
        delete _Data;
        _Data=std::exchange(other._Data,nullptr);
    }
    return *this;
}
Json::Value PreparedScene::ReleaseDocument() {
    Json::Value document;
    if(_Data) {
        document.swap(_Data->Document);
        delete std::exchange(_Data,nullptr);
    }
    return document;
}

bool Prepare(Json::Value&& document,PreparedScene& output,std::string& error) {
    error.clear();
    try {
        auto* data=new PreparedScene::Data(std::move(document));
        delete output._Data;
        output._Data=data;
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Scene: unexpected preparation failure";}
    return false;
}

bool RestoreComponent(const Json::Value& data,GameSproutRuntime::IGameObject& object,
                      GameSproutRuntime::IGameSproutRuntime& runtime,std::string& error) {
    error.clear();
    if(runtime.IsExecuting()) {error="Cannot restore a component inside a Runtime callback";return false;}
    const auto objectID=object.GetInstanceID();
    if(runtime.QueryGameObject(objectID)!=&object) {error="Component owner is not a live Runtime object";return false;}
    if(data.isObject()&&data["Type"].isString()&&ScenePhysicsDocument::IsCollider(data["Type"].asString())) {
        // Colliders are repeatable. Restoring one always creates a new instance;
        // the owning transaction restores its document ID and original slot.
        std::set<std::uint64_t> previous;
        for(std::size_t index=0;index<object.GetComponentCount();++index)
            previous.insert(object.GetComponentAt(index)->GetComponentInstanceID());
        bool open=false;
        try {
            ComponentData(data,nullptr,"Component");
            if(!runtime.BeginConstruction(error))return false;
            open=true;
            auto* restored=ScenePhysicsDocument::Create(data,object);
            const auto id=restored->GetComponentInstanceID();
            open=false;
            if(!runtime.EndConstruction(error))Fail("Component",error);
            if(runtime.QueryGameObject(objectID)!=&object)Fail("Component","owner destroyed during initialization");
            for(std::size_t index=0;index<object.GetComponentCount();++index)
                if(object.GetComponentAt(index)->GetComponentInstanceID()==id)return true;
            Fail("Component","restored Collider destroyed during initialization");
        } catch(const std::exception& exception){error=exception.what();}
        catch(...){error="Unexpected Collider restoration failure";}
        if(runtime.QueryGameObject(objectID)==&object) {
            std::vector<std::uint64_t> added;
            for(std::size_t index=0;index<object.GetComponentCount();++index) {
                const auto id=object.GetComponentAt(index)->GetComponentInstanceID();
                if(!previous.count(id))added.push_back(id);
            }
            for(auto id:added)if(!object.RemoveComponentInstance(id))error+="; Collider rollback failed";
        }
        FinishConstructionRollback(runtime,open,error);return false;
    }
    bool constructionOpen=false,changed=false;
    Json::Value previous;
    std::string type;
    std::uint64_t scriptID=0,previousAnimatorID=0;
    std::vector<GameSproutRuntime::AnimationNodeBinding> previousBindings;
    const auto find=[&]()->GameSproutRuntime::IComponent* {
        for(std::size_t index=0;index<object.GetComponentCount();++index) {
            auto* component=object.GetComponentAt(index);
            if(!component||!component->GetTypeName()||type!=component->GetTypeName())continue;
            if(type=="GameSproutRuntime.ScriptComponent") {
                const auto* script=dynamic_cast<GameSproutRuntime::IScriptComponent*>(component);
                if(!script||script->GetScriptInstanceID()!=scriptID)continue;
            }
            return component;
        }
        return nullptr;
    };
    try {
        ComponentData(data,nullptr,"Component");
        type=data["Type"].asString();
        if(type=="GameSproutRuntime.ScriptComponent")scriptID=data["InstanceID"].asUInt64();
        if(auto* existing=find()) {
            ObjectIDs snapshotIDs;
            if(const auto* animator=dynamic_cast<GameSproutRuntime::IAnimatorComponent*>(existing)) {
                previousBindings=animator->GetNodeBindings();
                for(const auto& binding:previousBindings)if(binding.ObjectID)
                    snapshotIDs.RuntimeToFile.emplace(binding.ObjectID,binding.ObjectID);
            } else if(const auto* skin=dynamic_cast<GameSproutRuntime::ISkinnedMeshRendererComponent*>(existing)) {
                previousAnimatorID=skin->GetAnimatorObjectID();
                if(previousAnimatorID)snapshotIDs.RuntimeToFile.emplace(previousAnimatorID,previousAnimatorID);
            }
            previous=SerializeComponent(*existing,snapshotIDs);
        }
        if(!runtime.BeginConstruction(error))return false;
        constructionOpen=true;changed=true;
        ComponentData(data,&object,"Component",&runtime);
        constructionOpen=false;
        if(!runtime.EndConstruction(error))Fail("Component",error.empty()?"Runtime could not finish component initialization":error);
        if(runtime.QueryGameObject(objectID)!=&object||!find())Fail("Component","Restored component was destroyed during initialization");
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Unexpected component restoration failure";}
    if(changed&&runtime.QueryGameObject(objectID)==&object) {
        // EndConstruction also closes a failed commit. Reopen only for rollback
        // so recreating/restoring authored fields cannot initialize halfway.
        if(!constructionOpen) {
            std::string cleanup;
            try {
                constructionOpen=runtime.BeginConstruction(cleanup);
                if(!constructionOpen)error+="; component rollback could not begin: "+cleanup;
            } catch(...) {error+="; component rollback could not begin";}
        }
        try {
            auto* current=find();
            if(previous.isNull()) {
                std::string cleanup;
                if(current && !(type=="GameSproutRuntime.ScriptComponent"?
                    runtime.RemoveScriptComponent(&object,scriptID,cleanup):runtime.RemoveBuiltinComponent(&object,type,cleanup)))
                    error+="; component rollback failed: "+cleanup;
            } else {
                ComponentData(previous,&object,"Component.Rollback",&runtime);
                current=find();
                if(auto* animator=dynamic_cast<GameSproutRuntime::IAnimatorComponent*>(current)) {
                    if(!animator->SetNodeBindings(previousBindings))Fail("Component.Rollback","Runtime rejected original animation bindings");
                } else if(auto* skin=dynamic_cast<GameSproutRuntime::ISkinnedMeshRendererComponent*>(current)) {
                    if(!skin->SetAnimatorObjectID(previousAnimatorID))Fail("Component.Rollback","Runtime rejected original Animator reference");
                }
            }
        } catch(const std::exception& exception) {error+="; component rollback failed: "+std::string(exception.what());}
        catch(...) {error+="; component rollback failed";}
    }
    FinishConstructionRollback(runtime,constructionOpen,error);
    return false;
}
Json::Value CreateDefault(const std::string& name) {
    Json::Value result(Json::objectValue);
    result["Version"]=Json::UInt(GameSproutRuntime::SceneDocumentVersion); result["Name"]=name; result["MainCameraID"]=Json::UInt64(1);
    Json::Value camera(Json::objectValue);
    camera["ID"]=Json::UInt64(1); camera["ParentID"]=Json::UInt64(0);
    camera["Name"]="Main Camera"; camera["Active"]=true;
    camera["Components"]=Json::Value(Json::arrayValue);
    Json::Value transform(Json::objectValue);
    transform["Type"]="GameSproutRuntime.Transform";
    transform["Position"]=Array({0,0,-3}); transform["Rotation"]=Array({0,0,0,1}); transform["Scale"]=Array({1,1,1});
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
bool ValidateSubtree(const Json::Value& document,std::string& error) {
    error.clear();
    try {ValidatePlan(document,false);return true;}
    catch(const std::exception& exception){error=exception.what();return false;}
}
bool ValidateProperties(const Json::Value& properties,bool sceneRoot,std::string& error) {
    if(sceneRoot) {
        auto document=properties;
        document["MainCameraID"]=Json::UInt64(0);
        document.removeMember("RootMainCamera");
        document.removeMember("Annotations");
        document["Objects"]=Json::Value(Json::arrayValue);
        return ValidateSubtree(document,error);
    }
    error.clear();
    try {
        std::vector<std::string_view> types;
        const auto node=ReadNode(properties,"Objects[0]",types);
        std::unordered_set<std::uint64_t> stableIDs{node.ID};
        auto maximum=node.ID;
        ValidateComponentIDs(*node.Components,stableIDs,maximum);
        return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Scene: unexpected property validation failure";}
    return false;
}
class SceneAssembly {
public:
    SceneAssembly(const SceneAssemblyPlan& plan,GameSproutRuntime::IGameSproutRuntime& runtime,bool incremental)
        :_Plan(plan),_Runtime(runtime),_BatchSize(incremental?256:std::numeric_limits<std::size_t>::max()) {}
    ~SceneAssembly() {
        if(!_Candidate&&!_ConstructionOpen)return;
        try {std::string ignored;Rollback(ignored);}
        catch(...) {}
    }

    bool Begin(std::string& error) {
        error.clear();
        if(_Runtime.IsExecuting()) {
            error="Scene: Runtime is executing an update, render, traversal or event callback";return false;
        }
        try {
            _Plan.ValidateTags(_Runtime.GetProjectSettings());
            _IDs.Next=_Plan.Values.Next;
            _ObjectBindings.reserve(std::min(_BatchSize,_Plan.Values.Nodes.size()+1));
            _ComponentBindings.reserve(std::min(_BatchSize,_Plan.Values.Nodes.size()+1));
            if(!_Runtime.BeginConstruction(error))return false;
            _ConstructionOpen=true;
            _Candidate=_Runtime.CreateScene(_Plan.Values.Name);
            if(!_Candidate)Fail("Scene","Runtime could not create scene");
            _CandidateID=_Candidate->GetInstanceID();
            if(!_Candidate->SetEnvironmentLighting(_Plan.Values.EnvironmentLighting))
                Fail("Scene.EnvironmentLighting","Runtime rejected environment lighting");
            return true;
        } catch(const std::exception& exception) {error=exception.what();}
        catch(...) {error="Scene: unexpected instantiation failure";}
        Rollback(error);
        return false;
    }

    AssemblyStatus Advance(std::chrono::steady_clock::time_point deadline,GameSproutRuntime::IScene*& output,
                           ObjectIDs& ids,std::string& error) {
        error.clear();
        if(!_Candidate||!_ConstructionOpen) {
            error="Scene: no open assembly";return AssemblyStatus::Failed;
        }
        if(_Runtime.IsExecuting()) {
            error="Scene: assembly must advance outside Runtime callbacks";return AssemblyStatus::Failed;
        }
        try {
            if(_Runtime.QueryGameObject(_CandidateID)!=_Candidate)Fail("Scene","candidate scene no longer exists");
            do {
                if(Step(error)) {
                    ids.RuntimeToFile.swap(_IDs.RuntimeToFile);ids.Next=_IDs.Next;
                    ids.SwapComponentIdentities(_IDs);
                    output=std::exchange(_Candidate,nullptr);
                    return AssemblyStatus::Complete;
                }
            } while(deadline==std::chrono::steady_clock::time_point::max()||std::chrono::steady_clock::now()<deadline);
            return AssemblyStatus::Pending;
        } catch(const std::exception& exception) {error=exception.what();}
        catch(...) {error="Scene: unexpected instantiation failure";}
        Rollback(error);
        return AssemblyStatus::Failed;
    }

private:
    enum class Stage { Objects,ObjectIdentities,Components,ComponentIdentities,Animation,Transforms,Finish };

    void Next(Stage stage) {_Stage=stage;_Index=0;}

    bool Step(std::string& error) {
        const auto& plan=_Plan.Values;
        switch(_Stage) {
        case Stage::Objects: {
            if(_Index==_Plan.ParentFirst.size()) {Next(Stage::ObjectIdentities);return false;}
            const auto& node=*_Plan.ParentFirst[_Index++];
            auto* parent=node.ParentID?_Objects.at(node.ParentID):_Candidate;
            auto* object=_Candidate->CreateGameObject(node.Name,parent);
            if(!object)Fail("Object "+std::to_string(node.ID),"Runtime could not create object");
            _Objects.emplace(node.ID,object);
            _IDs.RuntimeToFile.emplace(object->GetInstanceID(),node.ID);
            return false;
        }
        case Stage::ObjectIdentities: {
            if(!_Index&&_ObjectBindings.empty())_ObjectBindings.push_back({_CandidateID,0});
            while(_Index<plan.Nodes.size()&&_ObjectBindings.size()<_BatchSize) {
                const auto& node=plan.Nodes[_Index++];
                _ObjectBindings.push_back({_Objects.at(node.ID)->GetInstanceID(),node.ID});
            }
            if(!_Runtime.BindSceneObjectIdentities(_Candidate,_ObjectBindings,error))Fail("Scene.Identity",error);
            _ObjectBindings.clear();
            if(_Index==plan.Nodes.size())Next(Stage::Components);
            return false;
        }
        case Stage::Components: {
            if(_Index==plan.Nodes.size()) {
                if(plan.RootComponents)for(Json::ArrayIndex c=0;c<plan.RootComponents->size();++c)
                    ComponentData((*plan.RootComponents)[c],_Candidate,"Scene.RootComponents["+std::to_string(c)+"]",&_Runtime);
                ComponentIdentities(plan.RootComponents,*_Candidate,_IDs,&_ComponentBindings);
                Next(Stage::ComponentIdentities);
                return false;
            }
            const auto& node=plan.Nodes[_Index++];
            auto* object=_Objects.at(node.ID);
            const std::string path="Object "+std::to_string(node.ID)+".Components[";
            for(Json::ArrayIndex c=0;c<node.Components->size();++c)
                ComponentData((*node.Components)[c],object,path+std::to_string(c)+"]",&_Runtime);
            if(!object->SetLayer(node.Layer)||!object->SetTagID(node.TagID))
                Fail("Object.TagLayer","Runtime rejected Layer or undefined TagID");
            if(!object->SetStaticFlags(node.Static))Fail("Object.StaticFlags","Runtime rejected static authoring flags");
            object->SetActive(node.Active);
            return false;
        }
        case Stage::ComponentIdentities: {
            if(_Index<plan.Nodes.size()) {
                const auto& node=plan.Nodes[_Index++];
                ComponentIdentities(node.Components,*_Objects.at(node.ID),_IDs,&_ComponentBindings);
            }
            if(_ComponentBindings.size()>=_BatchSize||_Index==plan.Nodes.size()) {
                if(!_Candidate->BindAuthoringIdentities({},_ComponentBindings,error))Fail("Component.Identity",error);
                _ComponentBindings.clear();
            }
            if(_Index==plan.Nodes.size())Next(Stage::Animation);
            return false;
        }
        case Stage::Animation: {
            if(_Index<_Plan.Animation.size()) {
                const auto& binding=_Plan.Animation[_Index++];
                auto& owner=*_Objects.at(binding.Owner);
                if(ComponentType(*binding.Component)=="GameSproutRuntime.Animator")
                    SceneAnimationDocument::BindAnimator(*binding.Component,owner,_Objects);
                else SceneAnimationDocument::BindSkinnedRenderer(*binding.Component,owner,_Objects);
                return false;
            }
            if(!_Candidate->SetLayer(plan.RootLayer)||!_Candidate->SetTagID(plan.RootTagID))
                Fail("Scene.TagLayer","Runtime rejected root Layer or undefined TagID");
            if(!_Candidate->SetStaticFlags(plan.RootStatic))Fail("Scene.RootStaticFlags","Runtime rejected static authoring flags");
            _Candidate->SetActive(plan.RootActive);
            Next(Stage::Transforms);
            return false;
        }
        case Stage::Transforms: {
            if(_Index==plan.Nodes.size()) {Next(Stage::Finish);return false;}
            const auto& node=plan.Nodes[_Index++];
            const auto world=_Objects.at(node.ID)->GetTransform()->GetWorldMatrix();
            for(float value:world.Data)if(!std::isfinite(value))
                Fail("Object "+std::to_string(node.ID),"world transform overflows");
            return false;
        }
        case Stage::Finish: {
            auto* camera=plan.RootMainCamera?_Candidate->GetComponent<GameSproutRuntime::ICameraComponent>():
                plan.CameraID?_Objects.at(plan.CameraID)->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr;
            if(!_Candidate->SetMainCamera(camera))Fail("Scene.MainCameraID","Runtime rejected main camera");
            _ConstructionOpen=false;
            if(!_Runtime.EndConstruction(error))
                Fail("Scene",error.empty()?"Runtime could not finish scene initialization":error);
            if(_Runtime.QueryGameObject(_CandidateID)!=_Candidate)Fail("Scene","Scene was destroyed during initialization");
            return true;
        }
        }
        return false;
    }

    void Rollback(std::string& error) {
        auto* candidate=std::exchange(_Candidate,nullptr);
        if(candidate&&_Runtime.QueryGameObject(_CandidateID)==candidate) {
            try {
                if(!_Runtime.DestroyScene(candidate))error+="; candidate scene rollback was rejected";
            } catch(...) {error+="; candidate scene rollback failed";}
        }
        FinishConstructionRollback(_Runtime,_ConstructionOpen,error);
    }

    const SceneAssemblyPlan& _Plan;
    GameSproutRuntime::IGameSproutRuntime& _Runtime;
    const std::size_t _BatchSize;
    Stage _Stage=Stage::Objects;
    std::size_t _Index=0;
    GameSproutRuntime::IScene* _Candidate=nullptr;
    std::uint64_t _CandidateID=0;
    bool _ConstructionOpen=false;
    ObjectIDs _IDs;
    std::map<std::uint64_t,GameSproutRuntime::IGameObject*> _Objects;
    std::vector<GameSproutRuntime::SceneObjectIdentity> _ObjectBindings;
    std::vector<GameSproutRuntime::SceneComponentBinding> _ComponentBindings;
};

SceneAssembly* BeginAssembly(const PreparedScene& prepared,GameSproutRuntime::IGameSproutRuntime& runtime,std::string& error) {
    if(!prepared._Data) {error="Scene: no prepared document";return nullptr;}
    auto* assembly=new SceneAssembly(prepared._Data->Plan,runtime,true);
    if(assembly->Begin(error))return assembly;
    delete assembly;
    return nullptr;
}
AssemblyStatus AdvanceAssembly(SceneAssembly& assembly,std::chrono::steady_clock::time_point deadline,
                               GameSproutRuntime::IScene*& output,ObjectIDs& ids,std::string& error) {
    return assembly.Advance(deadline,output,ids,error);
}
void DestroyAssembly(SceneAssembly* assembly) noexcept {delete assembly;}

static bool InstantiatePlan(const SceneAssemblyPlan& plan,GameSproutRuntime::IGameSproutRuntime& runtime,
                     GameSproutRuntime::IScene*& output,ObjectIDs& ids,std::string& error) {
    SceneAssembly assembly(plan,runtime,false);
    return assembly.Begin(error)&&assembly.Advance(std::chrono::steady_clock::time_point::max(),output,ids,error)==AssemblyStatus::Complete;
}
bool Instantiate(const PreparedScene& prepared,GameSproutRuntime::IGameSproutRuntime& runtime,
                 GameSproutRuntime::IScene*& output,ObjectIDs& ids,std::string& error) {
    if(!prepared._Data) {error="Scene: no prepared document";return false;}
    return InstantiatePlan(prepared._Data->Plan,runtime,output,ids,error);
}
bool Instantiate(const Json::Value& document, GameSproutRuntime::IGameSproutRuntime& runtime,
                 GameSproutRuntime::IScene*& output, ObjectIDs& ids, std::string& error) {
    if(runtime.IsExecuting()) {
        error="Scene: Runtime is executing an update, render, traversal or event callback; synchronous scene loading is not allowed";
        return false;
    }
    try {
        const SceneAssemblyPlan plan(document);
        return InstantiatePlan(plan,runtime,output,ids,error);
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Scene: unexpected instantiation failure";}
    return false;
}

bool InstantiatePrefab(const Json::Value& document,GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IScene& scene,GameSproutRuntime::IGameObject& parent,
    GameSproutRuntime::IGameObject*& output,
    std::vector<GameSproutRuntime::PrefabObjectMapping>& mappings,std::string& error) {
    error.clear();
    if(runtime.IsExecuting()||runtime.QueryGameObject(scene.GetInstanceID())!=&scene||
       runtime.QueryGameObject(parent.GetInstanceID())!=&parent||parent.GetScene()!=&scene) {
        error="Prefab requires a live scene and parent outside Runtime callbacks";return false;
    }
    std::vector<std::uint64_t> created;bool constructionOpen=false;
    const auto sceneID=scene.GetInstanceID();
    try {
        if(!PrefabDocument::Validate(document,error))return false;
        const auto sourcePlan=ValidatePlan(document);
        ValidateProjectTags(sourcePlan,runtime);
        std::uint64_t maximum=0;
        std::vector<GameSproutRuntime::IGameObject*> pending{&scene};std::size_t count=0;
        while(!pending.empty()) {
            auto* object=pending.back();pending.pop_back();
            if(++count>MaximumObjects+1)Fail("Prefab","target scene is oversized");
            std::uint64_t id=0;if(runtime.GetSceneObjectFileID(object,id))maximum=std::max(maximum,id);
            for(std::size_t index=0;index<object->GetComponentCount();++index)
                if(runtime.GetSceneComponentFileID(object->GetComponentAt(index),id))maximum=std::max(maximum,id);
            for(std::size_t index=0;index<object->GetChildCount();++index)pending.push_back(object->GetChild(index));
        }
        if(count-1+sourcePlan.Nodes.size()>MaximumObjects)Fail("Prefab","combined scene exceeds object limit");
        std::size_t parentDepth=0;
        for(auto* object=&parent;object!=&scene;object=object->Parent())
            if(!object||++parentDepth>MaximumDepth)Fail("Prefab","invalid target parent hierarchy");
        for(const auto& node:sourcePlan.Nodes) {
            std::size_t depth=1;auto id=node.ParentID;
            while(id) {++depth;id=sourcePlan.Nodes.at(sourcePlan.Indices.at(id)).ParentID;}
            if(parentDepth+depth>MaximumDepth)Fail("Prefab","combined hierarchy exceeds depth limit");
        }
        std::map<std::uint64_t,std::uint64_t> remap;
        const auto allocate=[&](std::uint64_t source) {
            if(maximum==std::numeric_limits<std::uint64_t>::max())Fail("Prefab","scene identity space exhausted");
            remap.emplace(source,++maximum);
        };
        for(const auto& node:document["Objects"]) {
            allocate(node["ID"].asUInt64());
            for(const auto& component:node["Components"])if(component.isMember("ID"))allocate(component["ID"].asUInt64());
        }
        Json::Value translated=document;
        if(!PrefabDocument::Remap(translated,remap,error))return false;
        translated["NextObjectID"]=Json::UInt64(maximum==std::numeric_limits<std::uint64_t>::max()?0:maximum+1);
        const auto plan=ValidatePlan(translated);
        std::map<std::uint64_t,GameSproutRuntime::IGameObject*> objects;
        std::vector<GameSproutRuntime::PrefabObjectMapping> result;
        if(!runtime.BeginConstruction(error))return false;
        constructionOpen=true;
        for(const auto& node:plan.Nodes) {
            auto* object=scene.CreateGameObject(node.Name);
            if(!object)Fail("Prefab","Runtime could not create template node");
            created.push_back(object->GetInstanceID());objects.emplace(node.ID,object);
        }
        GameSproutRuntime::IGameObject* root=nullptr;
        for(const auto& node:plan.Nodes) {
            auto* object=objects.at(node.ID);auto* owner=node.ParentID?objects.at(node.ParentID):&parent;
            if(!owner->AddChild(object,false))Fail("Prefab","Runtime rejected template hierarchy");
            if(!node.ParentID)root=object;
        }
        std::vector<GameSproutRuntime::SceneObjectIdentity> identities;
        for(const auto& node:sourcePlan.Nodes) {
            auto* object=objects.at(remap.at(node.ID));
            identities.push_back({object->GetInstanceID(),remap.at(node.ID)});
            result.push_back({node.ID,object->GetInstanceID()});
        }
        if(!runtime.BindSceneObjectIdentities(&scene,identities,error))Fail("Prefab.Identity",error);
        ObjectIDs prefabIDs;
        prefabIDs.Next=translated["NextObjectID"].asUInt64();
        for(const auto& node:plan.Nodes) {
            auto* object=objects.at(node.ID);
            for(const auto& component:*node.Components)ComponentData(component,object,"Prefab.Component",&runtime);
            if(!object->SetLayer(node.Layer)||!object->SetTagID(node.TagID))
                Fail("Object.TagLayer","Runtime rejected Layer or undefined TagID");
            if(!object->SetStaticFlags(node.Static))Fail("Object.StaticFlags","Runtime rejected static authoring flags");
            object->SetActive(node.Active);
            ComponentIdentities(node.Components,*object,prefabIDs);
        }
        PublishComponentIdentities(scene,prefabIDs,runtime);
        SceneAnimationDocument::Bind(translated,scene,objects);
        for(const auto& node:plan.Nodes)for(float value:objects.at(node.ID)->GetTransform()->GetWorldMatrix().Data)
            if(!std::isfinite(value))Fail("Prefab.Transform","world transform overflows");
        const auto rootID=root->GetInstanceID();constructionOpen=false;
        if(!runtime.EndConstruction(error))Fail("Prefab",error.empty()?"Runtime could not finish template initialization":error);
        if(runtime.QueryGameObject(rootID)!=root)Fail("Prefab","template root was destroyed during initialization");
        for(const auto id:created)if(!runtime.QueryGameObject(id))Fail("Prefab","template node was destroyed during initialization");
        output=root;mappings.swap(result);return true;
    }catch(const std::exception& exception){error=exception.what();}
    catch(...){error="Unexpected Prefab assembly failure";}
    if(runtime.QueryGameObject(sceneID)==&scene)for(const auto id:created)
        if(auto* object=runtime.QueryGameObject(id)) {
            try {if(!scene.DestroyGameObject(object))error+="; Prefab rollback rejected";}
            catch(...){error+="; Prefab rollback failed";}
        }
    FinishConstructionRollback(runtime,constructionOpen,error);return false;
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
    bool constructionOpen=false;
    const auto sceneID=scene.GetInstanceID();
    const auto previousCamera=scene.GetMainCamera()?scene.GetMainCamera()->GetGameObject()->GetInstanceID():0;
    try {
        const Plan plan=ValidatePlan(document,false);
        ValidateProjectTags(plan,runtime);
        if(plan.Nodes.empty() || plan.RootMainCamera || (plan.RootComponents&&!plan.RootComponents->empty()))
            Fail("Subtree","expected nodes without scene-root components");
        const Node* root=nullptr;
        for(const auto& node:plan.Nodes) if(!node.ParentID) {
            if(root) Fail("Subtree","expected exactly one root");root=&node;
        }
        if(!root) Fail("Subtree","missing root");
        if(ids.RuntimeToFile.size()+plan.Nodes.size()>MaximumObjects)Fail("Subtree","combined scene exceeds object limit");
        std::size_t parentDepth=0;
        for(auto* object=&parent;object!=&scene;object=object->Parent())
            if(!object||++parentDepth+plan.Depth>MaximumDepth)Fail("Subtree","combined hierarchy exceeds depth limit");
        ObjectIDs candidateIDs;candidateIDs.Next=ids.Next;
        std::map<std::uint64_t,GameSproutRuntime::IGameObject*> objects;
        const auto checkIdentity=[&](std::uint64_t id) {
            if((ids.Next&&id>=ids.Next)||runtime.QuerySceneObjectByFileID(&scene,id)||runtime.QuerySceneComponentByFileID(&scene,id))
                Fail("Subtree.Identity","stable FileID is unavailable");
        };
        const auto checkTarget=[&](std::uint64_t id,bool needsAnimator) {
            if(!id)return;
            if(const auto internal=plan.Indices.find(id);internal!=plan.Indices.end()) {
                const auto& components=*plan.Nodes[internal->second].Components;
                if(needsAnimator&&std::none_of(components.begin(),components.end(),[](const auto& item){return item["Type"]=="GameSproutRuntime.Animator";}))
                    Fail("Subtree.Animation","target must contain an Animator");
                return;
            }
            auto* target=runtime.QuerySceneObjectByFileID(&scene,id);
            if(!target||(needsAnimator&&!target->GetComponent<GameSproutRuntime::IAnimatorComponent>()))
                Fail("Subtree.Animation","external binding is unavailable");
            objects.emplace(id,target);
        };
        for(const auto& node:plan.Nodes) {
            checkIdentity(node.ID);
            for(const auto& component:*node.Components) {
                if(component.isMember("ID"))checkIdentity(component["ID"].asUInt64());
                if(component["Type"]=="GameSproutRuntime.Animator")
                    for(const auto& binding:component["Bindings"])checkTarget(binding["ObjectFileID"].asUInt64(),false);
                else if(component["Type"]=="GameSproutRuntime.SkinnedMeshRenderer")
                    checkTarget(component["AnimatorObjectFileID"].asUInt64(),true);
            }
        }
        created.reserve(plan.Nodes.size());
        if(!runtime.BeginConstruction(error))return false;
        constructionOpen=true;
        // Restore directly into the final hierarchy. Source order determines
        // sibling order even when a child is listed before its parent.
        std::map<std::uint64_t,std::vector<const Node*>> children;
        for(const auto& node:plan.Nodes)if(node.ParentID)children[node.ParentID].push_back(&node);
        std::vector<const Node*> pending;
        pending.reserve(plan.Nodes.size());
        pending.push_back(root);
        for(std::size_t index=0;index<pending.size();++index) {
            const auto& node=*pending[index];
            auto* owner=node.ParentID?objects.at(node.ParentID):&parent;
            auto* object=scene.CreateGameObject(node.Name,owner);
            if(!object)Fail("Subtree","Runtime could not create object");
            created.push_back(object->GetInstanceID());
            objects.emplace(node.ID,object);
            candidateIDs.RuntimeToFile.emplace(object->GetInstanceID(),node.ID);
            if(const auto descendants=children.find(node.ID);descendants!=children.end())
                pending.insert(pending.end(),descendants->second.begin(),descendants->second.end());
        }
        std::vector<GameSproutRuntime::SceneObjectIdentity> identities;
        identities.reserve(plan.Nodes.size());
        for(const auto& node:plan.Nodes)identities.push_back({objects.at(node.ID)->GetInstanceID(),node.ID});
        if(!runtime.BindSceneObjectIdentities(&scene,identities,error))Fail("Subtree.Identity",error);
        std::vector<GameSproutRuntime::SceneComponentBinding> componentBindings;
        for(const auto& node:plan.Nodes) {
            auto* object=objects.at(node.ID);
            for(Json::ArrayIndex index=0;index<node.Components->size();++index)
                ComponentData((*node.Components)[index],object,"Subtree.Components",&runtime);
            if(!object->SetLayer(node.Layer)||!object->SetTagID(node.TagID))
                Fail("Object.TagLayer","Runtime rejected Layer or undefined TagID");
            if(!object->SetStaticFlags(node.Static))Fail("Object.StaticFlags","Runtime rejected static authoring flags");
            object->SetActive(node.Active);
            ComponentIdentities(node.Components,*object,candidateIDs,&componentBindings);
        }
        if(!scene.BindAuthoringIdentities({},componentBindings,error))Fail("Subtree.Identity",error);
        SceneAnimationDocument::Bind(document,scene,objects);
        auto* restored=objects.at(root->ID);
        if(!restored->SetSiblingIndex(siblingIndex))Fail("Subtree","Runtime rejected restored sibling order");
        if(plan.CameraID && !scene.SetMainCamera(objects.at(plan.CameraID)->GetComponent<GameSproutRuntime::ICameraComponent>()))
            Fail("Subtree","Runtime rejected restored main camera");
        for(const auto id:created) {
            auto* object=runtime.QueryGameObject(id);
            if(!object)Fail("Subtree","restored node is unavailable");
            for(const float value:object->GetTransform()->GetWorldMatrix().Data)
                if(!std::isfinite(value))Fail("Subtree","world transform overflows");
        }
        const auto restoredID=restored->GetInstanceID();
        constructionOpen=false;
        if(!runtime.EndConstruction(error))Fail("Subtree",error.empty()?"Runtime could not finish subtree initialization":error);
        if(runtime.QueryGameObject(restoredID)!=restored)Fail("Subtree","Restored root was destroyed during initialization");
        ids.MergeNewIdentities(candidateIDs);output=restored;return true;
    } catch(const std::exception& exception) {error=exception.what();}
    catch(...) {error="Unexpected subtree restoration failure";}
    if(runtime.QueryGameObject(sceneID)==&scene) {
        for(const auto id:created) if(auto* object=runtime.QueryGameObject(id)) {
            try {if(!scene.DestroyGameObject(object))error+="; subtree rollback rejected";}
            catch(...) {error+="; subtree rollback failed";}
        }
        auto* previous=previousCamera?runtime.QueryGameObject(previousCamera):nullptr;
        try {
            if(!scene.SetMainCamera(previous?previous->GetComponent<GameSproutRuntime::ICameraComponent>():nullptr))
                error+="; main camera rollback rejected";
        } catch(...) {error+="; main camera rollback failed";}
    }
    FinishConstructionRollback(runtime,constructionOpen,error);
    return false;
}
}

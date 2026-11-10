#include "ScenePhysicsDocument.h"
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
[[noreturn]] void Fail(const std::string& path,const std::string& reason){throw std::runtime_error(path+": "+reason);}
void Keys(const Json::Value& value,const std::string& path,std::initializer_list<const char*> allowed) {
    if(!value.isObject())Fail(path,"expected object");
    const auto last=value.end();
    for(auto member=value.begin();member!=last;++member) {
        const char* end=nullptr;
        const char* begin=member.memberName(&end);
        const std::string_view key(begin,static_cast<std::size_t>(end-begin));
        bool found=false;for(const char* name:allowed)if(key==name){found=true;break;}
        if(!found)Fail(path+"."+std::string(key),"unknown field");
    }
}
float Number(const Json::Value& value,const std::string& path) {
    if(!value.isNumeric()||!std::isfinite(value.asDouble())||std::fabs(value.asDouble())>std::numeric_limits<float>::max())Fail(path,"expected finite float");
    return value.asFloat();
}
float Number(const Json::Value& value,const char* name,float fallback,const std::string& path) {
    return value.isMember(name)?Number(value[name],path+"."+name):fallback;
}
bool Boolean(const Json::Value& value,const char* name,bool fallback,const std::string& path) {
    if(!value.isMember(name))return fallback;
    if(!value[name].isBool())Fail(path+"."+name,"expected bool");return value[name].asBool();
}
std::uint32_t Unsigned(const Json::Value& value,const std::string& path) {
    if(!value.isUInt64()||value.asUInt64()>std::numeric_limits<std::uint32_t>::max())Fail(path,"expected uint32");return value.asUInt();
}
GameSproutRuntime::Vector3 Vector(const Json::Value& value,const std::string& path) {
    if(!value.isArray()||value.size()!=3)Fail(path,"expected three numbers");
    return {Number(value[0],path),Number(value[1],path),Number(value[2],path)};
}
Json::Value Vector(GameSproutRuntime::Vector3 value) {
    Json::Value result(Json::arrayValue);result.append(value.X);result.append(value.Y);result.append(value.Z);return result;
}
GameSproutRuntime::ResourceReference Resource(const Json::Value& value,const std::string& path) {
    if(value.isNull())return {};
    Keys(value,path,{"Guid","LocalID"});
    if(!value["Guid"].isString()||value["Guid"].asString().size()!=32||!value["LocalID"].isUInt64()||!value["LocalID"].asUInt64())Fail(path,"invalid resource reference");
    std::string guid=value["Guid"].asString();
    for(char& c:guid) {
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))Fail(path,"invalid resource GUID");
        if(c>='A'&&c<='F')c=static_cast<char>(c-'A'+'a');
    }
    return {guid,value["LocalID"].asUInt64()};
}
Json::Value Resource(const GameSproutRuntime::ResourceReference& value) {
    if(value.Guid.empty())return {};
    Json::Value result;result["Guid"]=value.Guid;result["LocalID"]=Json::UInt64(value.LocalID);return result;
}
GameSproutRuntime::ColliderSettings Collider(const Json::Value& value,const std::string& path) {
    Keys(value,path,{"Enabled","IsTrigger","Center","Rotation","Material"});
    GameSproutRuntime::ColliderSettings settings;
    settings.Enabled=Boolean(value,"Enabled",true,path);settings.IsTrigger=Boolean(value,"IsTrigger",false,path);
    if(value.isMember("Center"))settings.Center=Vector(value["Center"],path+".Center");
    if(value.isMember("Rotation")) {
        const auto& rotation=value["Rotation"];
        if(!rotation.isArray()||rotation.size()!=4)Fail(path+".Rotation","expected four numbers");
        settings.Rotation={Number(rotation[0],path),Number(rotation[1],path),Number(rotation[2],path),Number(rotation[3],path)};
        GameSproutRuntime::Quaternion normalized;
        if(!settings.Rotation.TryNormalize(normalized))Fail(path+".Rotation","expected nonzero quaternion");
    }
    if(value.isMember("Material"))settings.Material=Resource(value["Material"],path+".Material");
    return settings;
}
GameSproutRuntime::RigidbodySettings Rigidbody(const Json::Value& value,const std::string& path) {
    Keys(value,path,{"Enabled","MotionType","Mass","GravityFactor","LinearDamping","AngularDamping","Constraints","Continuous"});
    GameSproutRuntime::RigidbodySettings settings;
    settings.Enabled=Boolean(value,"Enabled",true,path);settings.Continuous=Boolean(value,"Continuous",false,path);
    if(value.isMember("MotionType")) {
        if(value["MotionType"]=="Kinematic")settings.MotionType=GameSproutRuntime::PhysicsMotionType::Kinematic;
        else if(value["MotionType"]!="Dynamic")Fail(path+".MotionType","expected Dynamic or Kinematic");
    }
    settings.Mass=Number(value,"Mass",settings.Mass,path);settings.GravityFactor=Number(value,"GravityFactor",settings.GravityFactor,path);
    settings.LinearDamping=Number(value,"LinearDamping",settings.LinearDamping,path);settings.AngularDamping=Number(value,"AngularDamping",settings.AngularDamping,path);
    if(settings.Mass<=0||settings.LinearDamping<0||settings.AngularDamping<0)Fail(path,"mass must be positive and damping nonnegative");
    if(value.isMember("Constraints"))settings.Constraints=Unsigned(value["Constraints"],path+".Constraints");
    if(settings.Constraints&~63u)Fail(path+".Constraints","unknown constraint bits");
    return settings;
}
struct Settings {
    GameSproutRuntime::ColliderSettings Collider;
    GameSproutRuntime::RigidbodySettings Rigidbody;
    GameSproutRuntime::BoxColliderShape Box;
    GameSproutRuntime::SphereColliderShape Sphere;
    GameSproutRuntime::CapsuleColliderShape Capsule;
    GameSproutRuntime::MeshColliderShape Mesh;
};
Settings Read(const Json::Value& value,const std::string& path) {
    Keys(value,path,{"Type","Settings","Shape","ID"});
    if(!value["Type"].isString()||!ScenePhysicsDocument::Handles(value["Type"].asString()))Fail(path,"unknown physics component");
    const std::string type=value["Type"].asString();Settings result;
    if(type=="GameSproutRuntime.Rigidbody") {
        if(value.isMember("Shape"))Fail(path+".Shape","Rigidbody cannot contain collision geometry");
        if(value.isMember("Settings"))result.Rigidbody=Rigidbody(value["Settings"],path+".Settings");
        return result;
    }
    if(value.isMember("Settings"))result.Collider=Collider(value["Settings"],path+".Settings");
    const auto& shape=value["Shape"];
    if(type=="GameSproutRuntime.BoxCollider") {
        if(!shape.isNull()){Keys(shape,path+".Shape",{"Size"});if(shape.isMember("Size"))result.Box.Size=Vector(shape["Size"],path+".Shape.Size");}
        const auto size=result.Box.Size;if(size.X<=0||size.Y<=0||size.Z<=0)Fail(path+".Shape.Size","box dimensions must be positive");
    } else if(type=="GameSproutRuntime.SphereCollider") {
        if(!shape.isNull()){Keys(shape,path+".Shape",{"Radius"});result.Sphere.Radius=Number(shape,"Radius",result.Sphere.Radius,path+".Shape");}
        if(result.Sphere.Radius<=0)Fail(path+".Shape.Radius","sphere radius must be positive");
    } else if(type=="GameSproutRuntime.MeshCollider") {
        if(!shape.isNull()) {
            Keys(shape,path+".Shape",{"Mesh","Convex"});
            if(shape.isMember("Mesh"))result.Mesh.Mesh=Resource(shape["Mesh"],path+".Shape.Mesh");
            result.Mesh.Convex=Boolean(shape,"Convex",false,path+".Shape");
        }
        if(!result.Mesh.Convex&&result.Collider.IsTrigger)Fail(path+".Settings.IsTrigger","MeshCollider triggers require a convex shape");
    } else {
        if(!shape.isNull()) {
            Keys(shape,path+".Shape",{"Radius","Height","Axis"});
            result.Capsule.Radius=Number(shape,"Radius",result.Capsule.Radius,path+".Shape");result.Capsule.Height=Number(shape,"Height",result.Capsule.Height,path+".Shape");
            if(shape.isMember("Axis")) {
                if(shape["Axis"]=="X")result.Capsule.Axis=GameSproutRuntime::PhysicsAxis::X;
                else if(shape["Axis"]=="Z")result.Capsule.Axis=GameSproutRuntime::PhysicsAxis::Z;
                else if(shape["Axis"]!="Y")Fail(path+".Shape.Axis","expected X, Y or Z");
            }
        }
        if(result.Capsule.Radius<=0||result.Capsule.Height<2*result.Capsule.Radius)Fail(path+".Shape","capsule height must be at least twice its positive radius");
    }
    return result;
}
void ApplySettings(const Settings& settings,const std::string& type,GameSproutRuntime::IComponent& component) {
    if(type!=component.GetTypeName())Fail("PhysicsComponent","component type mismatch");
    if(type=="GameSproutRuntime.Rigidbody") {
        auto* body=dynamic_cast<GameSproutRuntime::IRigidbodyComponent*>(&component);
        if(!body||!body->SetSettings(settings.Rigidbody))Fail("PhysicsComponent","Runtime rejected Rigidbody settings");return;
    }
    auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(&component);
    if(!collider)Fail("PhysicsComponent","Collider interface mismatch");
    if(type=="GameSproutRuntime.BoxCollider") {
        auto* box=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(collider);
        if(!box||!box->SetShape(settings.Box))Fail("PhysicsComponent","Runtime rejected box shape");
    } else if(type=="GameSproutRuntime.SphereCollider") {
        auto* sphere=dynamic_cast<GameSproutRuntime::ISphereColliderComponent*>(collider);
        if(!sphere||!sphere->SetShape(settings.Sphere))Fail("PhysicsComponent","Runtime rejected sphere shape");
    } else if(type=="GameSproutRuntime.MeshCollider") {
        auto* mesh=dynamic_cast<GameSproutRuntime::IMeshColliderComponent*>(collider);
        // Disabling Convex and Trigger is one authored edit; do not validate the
        // new shape against the previous trigger flag during restoration.
        auto intermediate=collider->GetColliderSettings();intermediate.IsTrigger=false;
        if(!mesh||!collider->SetColliderSettings(intermediate)||!mesh->SetShape(settings.Mesh))Fail("PhysicsComponent","Runtime rejected mesh shape");
    } else {
        auto* capsule=dynamic_cast<GameSproutRuntime::ICapsuleColliderComponent*>(collider);
        if(!capsule||!capsule->SetShape(settings.Capsule))Fail("PhysicsComponent","Runtime rejected capsule shape");
    }
    if(!collider->SetColliderSettings(settings.Collider))Fail("PhysicsComponent","Runtime rejected Collider settings");
}
}
namespace ScenePhysicsDocument {
bool IsCollider(const std::string& type) {return type=="GameSproutRuntime.BoxCollider"||type=="GameSproutRuntime.SphereCollider"||type=="GameSproutRuntime.CapsuleCollider"||type=="GameSproutRuntime.MeshCollider";}
bool Handles(const std::string& type) {return type=="GameSproutRuntime.Rigidbody"||IsCollider(type);}
void Validate(const Json::Value& value,const std::string& path){Read(value,path);}
void Apply(const Json::Value& value,GameSproutRuntime::IComponent& component) {
    const auto settings=Read(value,"PhysicsComponent");
    ApplySettings(settings,value["Type"].asString(),component);
}
GameSproutRuntime::IComponent* Create(const Json::Value& value,GameSproutRuntime::IGameObject& object) {
    const auto settings=Read(value,"PhysicsComponent");
    const std::string type=value["Type"].asString();
    GameSproutRuntime::IComponent* component=nullptr;
    if(type=="GameSproutRuntime.Rigidbody")component=object.AddComponent<GameSproutRuntime::IRigidbodyComponent>();
    else if(type=="GameSproutRuntime.BoxCollider")component=object.AddComponent<GameSproutRuntime::IBoxColliderComponent>();
    else if(type=="GameSproutRuntime.SphereCollider")component=object.AddComponent<GameSproutRuntime::ISphereColliderComponent>();
    else if(type=="GameSproutRuntime.MeshCollider")component=object.AddComponent<GameSproutRuntime::IMeshColliderComponent>();
    else component=object.AddComponent<GameSproutRuntime::ICapsuleColliderComponent>();
    if(!component)Fail("PhysicsComponent","Runtime rejected physics component creation");
    ApplySettings(settings,type,*component);return component;
}
Json::Value Serialize(GameSproutRuntime::IComponent& component) {
    Json::Value value(Json::objectValue);value["Type"]=component.GetTypeName();
    if(auto* body=dynamic_cast<GameSproutRuntime::IRigidbodyComponent*>(&component)) {
        const auto& data=body->GetSettings();auto& settings=value["Settings"];
        settings["Enabled"]=data.Enabled;settings["MotionType"]=data.MotionType==GameSproutRuntime::PhysicsMotionType::Dynamic?"Dynamic":"Kinematic";
        settings["Mass"]=data.Mass;settings["GravityFactor"]=data.GravityFactor;
        settings["LinearDamping"]=data.LinearDamping;settings["AngularDamping"]=data.AngularDamping;
        settings["Constraints"]=Json::UInt(data.Constraints);settings["Continuous"]=data.Continuous;
    } else {
        auto* collider=dynamic_cast<GameSproutRuntime::IColliderComponent*>(&component);if(!collider)Fail("PhysicsComponent","Collider interface mismatch");
        const auto& data=collider->GetColliderSettings();auto& settings=value["Settings"];
        settings["Enabled"]=data.Enabled;settings["IsTrigger"]=data.IsTrigger;settings["Center"]=Vector(data.Center);
        Json::Value rotation(Json::arrayValue);rotation.append(data.Rotation.X);rotation.append(data.Rotation.Y);rotation.append(data.Rotation.Z);rotation.append(data.Rotation.W);settings["Rotation"]=rotation;
        settings["Material"]=Resource(data.Material);
        if(auto* box=dynamic_cast<GameSproutRuntime::IBoxColliderComponent*>(collider))value["Shape"]["Size"]=Vector(box->GetShape().Size);
        else if(auto* sphere=dynamic_cast<GameSproutRuntime::ISphereColliderComponent*>(collider))value["Shape"]["Radius"]=sphere->GetShape().Radius;
        else if(auto* capsule=dynamic_cast<GameSproutRuntime::ICapsuleColliderComponent*>(collider)) {
            const auto shape=capsule->GetShape();value["Shape"]["Radius"]=shape.Radius;value["Shape"]["Height"]=shape.Height;
            value["Shape"]["Axis"]=shape.Axis==GameSproutRuntime::PhysicsAxis::X?"X":shape.Axis==GameSproutRuntime::PhysicsAxis::Y?"Y":"Z";
        } else if(auto* mesh=dynamic_cast<GameSproutRuntime::IMeshColliderComponent*>(collider)) {
            const auto shape=mesh->GetShape();value["Shape"]["Mesh"]=Resource(shape.Mesh);value["Shape"]["Convex"]=shape.Convex;
        } else Fail("PhysicsComponent","unsupported Collider shape");
    }
    Validate(value,"PhysicsComponent");return value;
}
}

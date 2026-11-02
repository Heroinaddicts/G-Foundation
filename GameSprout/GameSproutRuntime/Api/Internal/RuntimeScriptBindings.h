#pragma once
// Compiler-generated bindings and host integration only. Gameplay sources use
// RuntimeScriptModule.h and GAMESPROUT_SCRIPT(Type).
#include "../RuntimeScriptModule.h"
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <type_traits>

#if defined(_WIN32)
#define GAMESPROUT_SCRIPT_EXPORT __declspec(dllexport)
#define GAMESPROUT_SCRIPT_LOCAL
#else
#define GAMESPROUT_SCRIPT_EXPORT __attribute__((visibility("default")))
#define GAMESPROUT_SCRIPT_LOCAL __attribute__((visibility("hidden")))
#endif

namespace GameSproutRuntime::Internal {
template<typename T> ScriptValue ScriptValueOf(const T& value) {
    ScriptValue result;
    if constexpr (std::is_same_v<T,bool>) { result.Kind=ScriptValueKind::Boolean; result.Boolean=value; }
    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) { result.Kind=ScriptValueKind::Integer; result.Integer=value; }
    else if constexpr (std::is_floating_point_v<T>) { result.Kind=ScriptValueKind::Number; result.Number=value; }
    else if constexpr (std::is_same_v<T,std::string>) { result.Kind=ScriptValueKind::String; result.String=value; }
    else if constexpr (std::is_same_v<T,Vector3>) { result.Kind=ScriptValueKind::Vector3; result.Vector=value; }
    else if constexpr (std::is_same_v<T,ResourceReference>) { result.Kind=ScriptValueKind::Resource; result.Resource=value; }
    else if constexpr (std::is_same_v<T,IGameObject*>) {
        if(value)throw std::invalid_argument("Serialized object reference defaults must be null");
        result.Kind=ScriptValueKind::SceneObject;
    }
    else static_assert(!sizeof(T),"Unsupported script field type");
    return result;
}
template<typename T> bool ReadScriptValue(const ScriptValue& value,T& output,std::string& error) {
    if constexpr (std::is_same_v<T,IGameObject*>) {
        error="Object references require Runtime identity resolution";return false;
    }
    if(value.Kind!=ScriptValueOf(T{}).Kind) {error="Script field value has the wrong kind";return false;}
    if constexpr (std::is_same_v<T,bool>) output=value.Boolean;
    else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        if(value.Integer<std::numeric_limits<T>::min()||value.Integer>std::numeric_limits<T>::max()) {error="Integer is out of range";return false;}
        output=static_cast<T>(value.Integer);
    } else if constexpr (std::is_floating_point_v<T>) {
        if(!std::isfinite(value.Number)||std::fabs(value.Number)>std::numeric_limits<T>::max()) {error="Number is not finite or out of range";return false;}
        output=static_cast<T>(value.Number);
    } else if constexpr (std::is_same_v<T,std::string>) output=value.String;
    else if constexpr (std::is_same_v<T,Vector3>) {
        if(!std::isfinite(value.Vector.X)||!std::isfinite(value.Vector.Y)||!std::isfinite(value.Vector.Z)) {error="Vector is not finite";return false;}
        output=value.Vector;
    } else if constexpr (std::is_same_v<T,ResourceReference>) output=value.Resource;
    error.clear();return true;
}
template<typename C,typename T>
ScriptFieldRegistration MakeField(const std::string& id,const std::string& name,T C::* member,
    const std::type_identity_t<T>& defaultValue,ScriptResourceKind resourceKind=ScriptResourceKind::Any,
    const std::string& declaringType={}) {
    ScriptFieldRegistration result;
    result.Info={id,name,ScriptValueOf(defaultValue),resourceKind};
    result.Info.DeclaringType=declaringType;
    if constexpr (std::is_same_v<T,IGameObject*>) {
        result.WriteObject=[member](IScript& instance,IGameObject* target,std::string& error) {
            C* object=dynamic_cast<C*>(&instance);
            if(!object) {error="Script field instance type mismatch";return false;}
            object->*member=target;error.clear();return true;
        };
        result.ReadObject=[member](const IScript& instance)->IGameObject* {
            const C* object=dynamic_cast<const C*>(&instance);
            return object?object->*member:nullptr;
        };
    } else {
    result.Write=[member](IScript& instance,const ScriptValue& value,std::string& error) {
        C* object=dynamic_cast<C*>(&instance);
        if(!object) {error="Script field instance type mismatch";return false;}
        return ReadScriptValue(value,object->*member,error);
    };
    result.Read=[member](const IScript& instance,ScriptValue& value,std::string& error) {
        const C* object=dynamic_cast<const C*>(&instance);
        if(!object) {error="Script field instance type mismatch";return false;}
        value=ScriptValueOf(object->*member);error.clear();return true;
    };
    }
    return result;
}
// Hidden visibility is essential: each loaded project library owns its own
// collector; inline symbols must not be interposed by another loaded library.
GAMESPROUT_SCRIPT_LOCAL inline std::vector<ScriptTypeRegistration>& ScriptModuleTypes() {
    static std::vector<ScriptTypeRegistration> types;
    return types;
}
template<typename T> GAMESPROUT_SCRIPT_LOCAL bool RegisterScriptClass(const char* id,const char* name,const char* file,
    const ResourceReference& source,std::vector<ScriptFieldRegistration> fields={}) {
    static_assert(std::is_base_of_v<ScriptBehaviour,T>);
    static_assert(std::is_default_constructible_v<T>,"Script components must be default constructible");
    ScriptTypeRegistration type;
    type.Info.TypeID=id;type.Info.Name=name;type.Info.SourceFile=file;
    type.CppType=&typeid(T);type.Source=source;
    type.Create=[](IGameObject* owner)->IScript* {
        ScriptConstructionScope construction(owner);
        return new T();
    };
    type.Destroy=[](IScript* instance) {delete static_cast<T*>(instance);};
    type.Fields=std::move(fields);
    for(const auto& field:type.Fields) type.Info.Fields.push_back(field.Info);
    ScriptModuleTypes().push_back(std::move(type));
    return true;
}
GAMESPROUT_SCRIPT_LOCAL inline const ScriptModuleAPI* GetScriptModuleAPI(const char* buildID=nullptr) noexcept {
    static ScriptModuleAPI api;
    api.BuildID=buildID;
    api.Types=ScriptModuleTypes().data();api.Count=ScriptModuleTypes().size();
    return &api;
}
}


#define GAMESPROUT_GAME_MODULE() \
    extern "C" GAMESPROUT_SCRIPT_EXPORT const ::GameSproutRuntime::ScriptModuleAPI* GetGameSproutGameLogic() noexcept { \
        return ::GameSproutRuntime::Internal::GetScriptModuleAPI(); \
    }

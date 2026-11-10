#pragma once
// Compiler-generated bindings and host integration only. Gameplay sources use
// RuntimeScriptModule.h and a declaration marker.
#include "../RuntimeScriptModule.h"
#include "ScriptNativeValues.h"
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
template<typename T> ScriptValue ScriptValueOf(const T& input) {
    ScriptValue value;std::string error;
    if(!ReadNativeValue(input,value,{},error))throw std::invalid_argument(error);
    return value;
}
template<typename T> bool ReadScriptValue(const ScriptValue& value,T& output,std::string& error) {
    return WriteNativeValue(output,value,{},error);
}
template<typename C,typename T>
ScriptFieldRegistration MakeField(const std::string& id,const std::string& name,T C::* member,ScriptFieldInfo schema) {
    ScriptFieldRegistration result;schema.ID=id;schema.Name=name;result.Info=std::move(schema);
    result.Write=[member](IScript& instance,const ScriptValue& value,const ScriptReferenceContext& context,std::string& error) {
        C* object=dynamic_cast<C*>(&instance);
        if(!object){error="Script field instance type mismatch";return false;}
        return WriteNativeValue(object->*member,value,context,error);
    };
    result.Read=[member](const IScript& instance,ScriptValue& value,const ScriptReferenceContext& context,std::string& error) {
        const C* object=dynamic_cast<const C*>(&instance);
        if(!object){error="Script field instance type mismatch";return false;}
        return ReadNativeValue(object->*member,value,context,error);
    };
    result.Validate=[](const ScriptValue& value,const ScriptReferenceContext& context,std::string& error) {
        return ValidateNativeValue<T>(value,context,error);
    };
    return result;
}
template<typename C,typename T>
ScriptFieldRegistration MakeField(const std::string& id,const std::string& name,T C::* member,
    const std::type_identity_t<T>& defaultValue,ScriptResourceKind resourceKind=ScriptResourceKind::Any,
    const std::string& declaringType={}) {
    ScriptFieldInfo schema;schema.Default=ScriptValueOf(defaultValue);schema.ResourceKind=resourceKind;schema.DeclaringType=declaringType;
    return MakeField<C>(id,name,member,std::move(schema));
}
// Hidden visibility is essential: each loaded project library owns its own
// collector; inline symbols must not be interposed by another loaded library.
GAMESPROUT_SCRIPT_LOCAL inline std::vector<ScriptTypeRegistration>& ScriptModuleTypes() {
    static std::vector<ScriptTypeRegistration> types;
    return types;
}
GAMESPROUT_SCRIPT_LOCAL inline std::vector<GameModuleRegistration>& GameModuleTypes() {
    static std::vector<GameModuleRegistration> modules;
    return modules;
}
template<typename T> GAMESPROUT_SCRIPT_LOCAL bool RegisterScriptClass(const char* id,const char* name,const char* file,
    const ResourceReference& source,std::vector<ScriptFieldRegistration> fields={});
template<typename T> GAMESPROUT_SCRIPT_LOCAL bool RegisterGameModuleClass(const char* name,const char* guid,
    const char* file);

template<typename T> struct GameplayFactory {
private:
    friend bool RegisterScriptClass<T>(const char*,const char*,const char*,const ResourceReference&,
        std::vector<ScriptFieldRegistration>);
    friend bool RegisterGameModuleClass<T>(const char*,const char*,const char*);

    static T* Create(IGameSproutRuntime* runtime) {
        constexpr bool canConstruct = requires { new T(runtime); };
        static_assert(canConstruct,
            "Gameplay types need a constructor taking IGameSproutRuntime*. A private constructor "
            "requires friendship with GameSproutRuntime::Internal::GameplayFactory<T>.");
        if constexpr (canConstruct) return new T(runtime);
    }
};

template<typename T> GAMESPROUT_SCRIPT_LOCAL bool RegisterScriptClass(const char* id,const char* name,const char* file,
    const ResourceReference& source,std::vector<ScriptFieldRegistration> fields) {
    static_assert(std::is_base_of_v<ScriptBehaviour,T>);
    ScriptTypeRegistration type;
    type.Info.TypeID=id;type.Info.Name=name;type.Info.SourceFile=file;
    type.CppType=&typeid(T);type.Source=source;
    type.Create=[](IGameObject* owner,IGameSproutRuntime* runtime)->IScript* {
        ScriptConstructionScope construction(owner);
        return GameplayFactory<T>::Create(runtime);
    };
    type.Destroy=[](IScript* instance) {delete static_cast<T*>(instance);};
    type.Fields=std::move(fields);
    for(const auto& field:type.Fields) type.Info.Fields.push_back(field.Info);
    ScriptModuleTypes().push_back(std::move(type));
    return true;
}
template<typename T> GAMESPROUT_SCRIPT_LOCAL bool RegisterGameModuleClass(const char* name, const char* guid,
    const char* file) {
    static_assert(std::is_convertible_v<T*, IModule*>, "Gameplay modules must publicly derive from IModule");
    GameModuleRegistration module;
    module.Name = name;
    module.SourceGuid = guid;
    module.SourceFile = file;
    module.Create = [](IGameSproutRuntime* runtime) -> IModule* { return GameplayFactory<T>::Create(runtime); };
    module.Destroy = [](IModule* instance) { delete instance; };
    GameModuleTypes().push_back(std::move(module));
    return true;
}
GAMESPROUT_SCRIPT_LOCAL inline const ScriptModuleAPI* GetScriptModuleAPI(const char* buildID=nullptr) noexcept {
    static ScriptModuleAPI api;
    api.BuildID=buildID;
    api.Types=ScriptModuleTypes().data();api.Count=ScriptModuleTypes().size();
    api.Modules=GameModuleTypes().data();api.ModuleCount=GameModuleTypes().size();
    return &api;
}
}


#define GAMESPROUT_GAME_MODULE() \
    extern "C" GAMESPROUT_SCRIPT_EXPORT const ::GameSproutRuntime::ScriptModuleAPI* GetGameSproutGameLogic() noexcept { \
        return ::GameSproutRuntime::Internal::GetScriptModuleAPI(); \
    }

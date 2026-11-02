#pragma once
#include "GameSproutRuntimeApi.h"
#include <map>
#include <utility>

namespace GameSproutRuntime {
enum class ScriptValueKind : std::uint8_t { Boolean, Integer, Number, String, Vector3, Resource, SceneObject };
enum class ScriptResourceKind : std::uint8_t { Any, Texture, Mesh, Material, AnimationClip, AnimatorController, Skeleton, Avatar, Audio };
// Authored identity within the owner's scene document. No address or Runtime
// InstanceID is persisted. Assigned distinguishes None from the scene root (0).
struct ScriptObjectReference {
    bool Assigned = false;
    std::uint64_t ObjectFileID = 0;
    bool operator==(const ScriptObjectReference&) const = default;
};
struct ScriptValue {
    ScriptValueKind Kind = ScriptValueKind::Number;
    bool Boolean = false;
    std::int64_t Integer = 0;
    double Number = 0;
    std::string String;
    GameSproutRuntime::Vector3 Vector;
    ResourceReference Resource;
    ScriptObjectReference SceneObject;
    bool operator==(const ScriptValue& value) const {
        if (Kind != value.Kind) return false;
        switch (Kind) {
        case ScriptValueKind::Boolean: return Boolean == value.Boolean;
        case ScriptValueKind::Integer: return Integer == value.Integer;
        case ScriptValueKind::Number: return Number == value.Number;
        case ScriptValueKind::String: return String == value.String;
        case ScriptValueKind::Vector3: return Vector.X == value.Vector.X && Vector.Y == value.Vector.Y && Vector.Z == value.Vector.Z;
        case ScriptValueKind::Resource: return Resource == value.Resource;
        case ScriptValueKind::SceneObject: return SceneObject == value.SceneObject;
        }
        return false;
    }
};
struct ScriptFieldInfo {
    std::string ID, Name;
    ScriptValue Default;
    ScriptResourceKind ResourceKind = ScriptResourceKind::Any;
    std::string DeclaringType;
};
struct ScriptTypeInfo {
    std::string TypeID, Name, SourceFile;
    std::vector<ScriptFieldInfo> Fields;
};
struct ScriptFieldRegistration {
    ScriptFieldInfo Info;
    std::function<bool(IScript&, const ScriptValue&, std::string&)> Write;
    std::function<bool(const IScript&, ScriptValue&, std::string&)> Read;
    // Native-module-local bridges. These functions and pointers never enter
    // scene files, field schemas, or the future Player transport.
    std::function<IGameObject*(const IScript&)> ReadObject;
    std::function<bool(IScript&, IGameObject*, std::string&)> WriteObject;
};
struct ScriptTypeRegistration {
    ScriptTypeInfo Info;
    // Borrowed from the loaded native module; only type_info value equality is used.
    const std::type_info* CppType = nullptr;
    ResourceReference Source;
    IScript* (*Create)(IGameObject*) = nullptr;
    void (*Destroy)(IScript*) = nullptr;
    std::vector<ScriptFieldRegistration> Fields;
};
struct BuiltinComponentInfo {
    std::string TypeID, Name, Category;
    bool Intrinsic = false;
    bool AllowMultiple = false;
};

// This is the authored component. The native gameplay instance is created only
// when execution is explicitly enabled, after field values have been restored.
class GAMESPROUT_RUNTIME_PUBLIC_TYPE IScriptComponent : public IComponent {
public:
    using IComponent::IComponent;
    virtual std::uint64_t GetScriptInstanceID() const = 0;
    virtual const std::string& GetScriptTypeID() const = 0;
    virtual ResourceReference GetScriptSource() const = 0;
    virtual bool IsScriptTypeAvailable() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool) = 0;
    virtual const std::map<std::string, ScriptValue>& GetFields() const = 0;
    virtual bool SetField(const std::string&, const ScriptValue&, std::string& error) = 0;
    // Exact authored snapshot replacement before a native instance starts.
    // Rejected values leave every field unchanged; omitted fields are removed.
    virtual bool ReplaceFields(const std::map<std::string, ScriptValue>&, std::string& error) {
        error="Script field replacement is unavailable";return false;
    }
    // Restore a persisted document against the current declared field schema.
    // Matching IDs/kinds retain their values; missing or changed kinds use the
    // declared defaults, and removed fields are ignored. Unknown types retain
    // the complete snapshot until their registration becomes available.
    virtual bool RestoreSerializedFields(const std::map<std::string, ScriptValue>& values, std::string& error) {
        return ReplaceFields(values,error);
    }
    virtual const std::string& GetScriptError() const = 0;
};

inline std::vector<BuiltinComponentInfo> IGameSproutRuntime::GetBuiltinComponentTypes() const { return {}; }
inline IComponent* IGameSproutRuntime::AddBuiltinComponent(IGameObject*,const std::string&,std::string& error) { error="Component creation is unavailable"; return nullptr; }
inline bool IGameSproutRuntime::RemoveBuiltinComponent(IGameObject*,const std::string&,std::string& error) { error="Component removal is unavailable"; return false; }
inline bool IGameSproutRuntime::RegisterScriptType(const ScriptTypeRegistration&,std::string& error) { error="Native scripts are unavailable"; return false; }
inline bool IGameSproutRuntime::ReplaceScriptTypes(const std::vector<std::string>&,const std::vector<ScriptTypeRegistration>&,std::string& error) { error="Native scripts are unavailable"; return false; }
inline bool IGameSproutRuntime::UnregisterScriptType(const std::string&,std::string& error) { error="Native scripts are unavailable"; return false; }
inline std::vector<ScriptTypeInfo> IGameSproutRuntime::GetScriptTypes() const { return {}; }
inline IScriptComponent* IGameSproutRuntime::AddScriptComponent(IGameObject*,const std::string&,const ResourceReference&,std::uint64_t,std::string& error) { error="Native scripts are unavailable"; return nullptr; }
inline bool IGameSproutRuntime::RemoveScriptComponent(IGameObject*,std::uint64_t,std::string& error) { error="Native scripts are unavailable"; return false; }
inline bool IGameSproutRuntime::SetScriptExecutionEnabled(bool,std::string& error) { error="Native scripts are unavailable"; return false; }

// Fixed native library entry contract. All SDK/compiler/CRT versions must match.
inline constexpr std::uint32_t ScriptModuleABIVersion = 3;
inline constexpr const char* ScriptModuleEntryName = "GetGameSproutGameLogic";
struct ScriptModuleAPI {
    std::uint32_t Version = ScriptModuleABIVersion;
    const ScriptTypeRegistration* Types = nullptr;
    std::size_t Count = 0;
    const char* BuildID = nullptr;
};
using fGetScriptModule = const ScriptModuleAPI* (*)() noexcept;
}

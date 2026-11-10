#pragma once
#include "GameSproutRuntimeApi.h"
#include <map>
#include <utility>

namespace GameSproutRuntime {
enum class ScriptValueKind : std::uint8_t { Boolean, Integer, Number, String, Vector3, Resource, SceneObject, Unsigned, Component, Structure, Sequence, Dictionary, Vector2, Vector4, Quaternion, Color };
enum class ScriptResourceKind : std::uint8_t { Any, Texture, Mesh, Material, AnimationClip, AnimatorController, Skeleton, Avatar, Audio };
// Authored identity within the owner's scene document, or explicit session-only
// Runtime IDs. Runtime IDs never enter persisted data. Assigned distinguishes
// None from the scene root (ObjectFileID 0 without Runtime IDs).
struct ScriptObjectReference {
    bool Assigned = false;
    std::uint64_t ObjectFileID = 0;
    // Session-only reference for a gameplay object without a document FileID.
    // Both Runtime IDs must be nonzero, ObjectFileID must be zero. Never persist it.
    std::uint64_t RuntimeSceneID = 0;
    std::uint64_t RuntimeObjectID = 0;
    bool operator==(const ScriptObjectReference&) const = default;
};
struct ScriptComponentReference {
    bool Assigned = false;
    std::uint64_t ComponentFileID = 0;
    std::uint64_t RuntimeSceneID = 0;
    std::uint64_t RuntimeObjectID = 0;
    std::uint64_t RuntimeComponentID = 0;
    bool operator==(const ScriptComponentReference&) const = default;
};
// Native-module-local callbacks. Only value identities are persisted or transported.
struct ScriptReferenceContext {
    std::function<bool(const IGameObject*, ScriptObjectReference&, std::string&)> EncodeObject;
    std::function<bool(const ScriptObjectReference&, IGameObject*&, std::string&)> DecodeObject;
    std::function<bool(const IComponent*, ScriptComponentReference&, std::string&)> EncodeComponent;
    std::function<bool(const ScriptComponentReference&, bool, IComponent*&, std::string&)> DecodeComponent;
};
struct ScriptValue {
    ScriptValueKind Kind = ScriptValueKind::Number;
    bool Boolean = false;
    std::int64_t Integer = 0;
    std::uint64_t Unsigned = 0;
    double Number = 0;
    std::string String;
    GameSproutRuntime::Vector3 Vector;
    ResourceReference Resource;
    ScriptObjectReference SceneObject;
    ScriptComponentReference Component;
    GameSproutRuntime::Vector2 Vector2;
    GameSproutRuntime::Vector4 Vector4;
    GameSproutRuntime::Quaternion Rotation;
    GameSproutRuntime::Color Tint;
    std::map<std::string, ScriptValue> Fields;
    std::vector<ScriptValue> Elements;
    std::vector<std::pair<ScriptValue, ScriptValue>> Entries;
    bool operator==(const ScriptValue& value) const {
        if (Kind != value.Kind) return false;
        switch (Kind) {
        case ScriptValueKind::Boolean: return Boolean == value.Boolean;
        case ScriptValueKind::Integer: return Integer == value.Integer;
        case ScriptValueKind::Unsigned: return Unsigned == value.Unsigned;
        case ScriptValueKind::Number: return Number == value.Number;
        case ScriptValueKind::String: return String == value.String;
        case ScriptValueKind::Vector3: return Vector.X == value.Vector.X && Vector.Y == value.Vector.Y && Vector.Z == value.Vector.Z;
        case ScriptValueKind::Resource: return Resource == value.Resource;
        case ScriptValueKind::SceneObject: return SceneObject == value.SceneObject;
        case ScriptValueKind::Component: return Component == value.Component;
        case ScriptValueKind::Structure: return Fields == value.Fields;
        case ScriptValueKind::Sequence: return Elements == value.Elements;
        case ScriptValueKind::Dictionary: return Entries == value.Entries;
        case ScriptValueKind::Vector2: return Vector2.X==value.Vector2.X && Vector2.Y==value.Vector2.Y;
        case ScriptValueKind::Vector4: return Vector4.X==value.Vector4.X && Vector4.Y==value.Vector4.Y && Vector4.Z==value.Vector4.Z && Vector4.W==value.Vector4.W;
        case ScriptValueKind::Quaternion: return Rotation.X==value.Rotation.X && Rotation.Y==value.Rotation.Y && Rotation.Z==value.Rotation.Z && Rotation.W==value.Rotation.W;
        case ScriptValueKind::Color: return Tint.R==value.Tint.R && Tint.G==value.Tint.G && Tint.B==value.Tint.B && Tint.A==value.Tint.A;
        }
        return false;
    }
};
struct ScriptEnumValue {
    std::string Name;
    std::int64_t Value = 0;
    bool IsUnsigned = false;
    std::uint64_t UnsignedValue = 0;
};
struct ScriptFieldInfo {
    std::string ID, Name;
    ScriptValue Default;
    ScriptResourceKind ResourceKind = ScriptResourceKind::Any;
    std::string DeclaringType;
    std::string TypeName, ComponentType;
    std::int64_t FixedLength = -1; // Sequence: -1 dynamic; >=0 exact length.
    std::vector<ScriptFieldInfo> Children;
    std::vector<ScriptEnumValue> EnumValues;
};
struct ScriptTypeInfo {
    std::string TypeID, Name, SourceFile;
    std::vector<ScriptFieldInfo> Fields;
};
struct ScriptFieldRegistration {
    ScriptFieldInfo Info;
    std::function<bool(IScript&, const ScriptValue&, const ScriptReferenceContext&, std::string&)> Write;
    std::function<bool(const IScript&, ScriptValue&, const ScriptReferenceContext&, std::string&)> Read;
    std::function<bool(const ScriptValue&, const ScriptReferenceContext&, std::string&)> Validate;
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
struct GameModuleTypeInfo {
    std::string Name;
    std::string SourceGuid;
    std::string SourceFile;
    bool operator==(const GameModuleTypeInfo&) const = default;
};
struct GameModuleRegistration {
    std::string Name;
    std::string SourceGuid;
    std::string SourceFile;
    IModule* (*Create)() = nullptr;
    void (*Destroy)(IModule*) = nullptr;
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
    // A value snapshot of the actual native instance, or authored fields before
    // execution starts. Does not replace the authored serialization baseline.
    virtual bool ReadFields(std::map<std::string,ScriptValue>& snapshot,std::string& error) const {
        snapshot=GetFields();error.clear();return true;
    }
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
inline bool IGameSproutRuntime::ReplaceGameLogicTypes(const std::vector<std::string>& removeScriptTypes,
    const std::vector<ScriptTypeRegistration>& scripts, const std::vector<GameModuleRegistration>& modules,
    std::string& error) {
    if (modules.empty()) return ReplaceScriptTypes(removeScriptTypes, scripts, error);
    error = "Native gameplay modules are unavailable";
    return false;
}
inline bool IGameSproutRuntime::UnregisterScriptType(const std::string&,std::string& error) { error="Native scripts are unavailable"; return false; }
inline std::vector<ScriptTypeInfo> IGameSproutRuntime::GetScriptTypes() const { return {}; }
inline IScriptComponent* IGameSproutRuntime::AddScriptComponent(IGameObject*,const std::string&,const ResourceReference&,std::uint64_t,std::string& error) { error="Native scripts are unavailable"; return nullptr; }
inline bool IGameSproutRuntime::RemoveScriptComponent(IGameObject*,std::uint64_t,std::string& error) { error="Native scripts are unavailable"; return false; }
inline bool IGameSproutRuntime::SetScriptExecutionEnabled(bool,std::string& error) { error="Native scripts are unavailable"; return false; }

// Fixed native library entry contract. All SDK/compiler/CRT versions must match.
inline constexpr std::uint32_t ScriptModuleABIVersion = 6;
inline constexpr const char* ScriptModuleEntryName = "GetGameSproutGameLogic";
struct ScriptModuleAPI {
    std::uint32_t Version = ScriptModuleABIVersion;
    const ScriptTypeRegistration* Types = nullptr;
    std::size_t Count = 0;
    const char* BuildID = nullptr;
    const GameModuleRegistration* Modules = nullptr;
    std::size_t ModuleCount = 0;
};
using fGetScriptModule = const ScriptModuleAPI* (*)() noexcept;
}

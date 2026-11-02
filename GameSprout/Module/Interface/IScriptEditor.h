#pragma once
#include "GameSproutEditorApi.h"

struct EditorScriptType {
    GameSproutRuntime::ScriptTypeInfo Type;
    GameSproutRuntime::ResourceReference Source;
    std::string Path; // Relative to Assets.
};
struct ScriptBuildState {
    bool Compiling = false;
    bool HasSuccessfulBuild = false;
    bool PendingChanges = false;
    bool ReloadBlocked = false;
    std::string Error;
    // Advances only for source-asset events, not explicit compile requests.
    uint64_t SourceRevision = 0;
};
// Value-only editor commands. Session changes on a scene/world switch; a stale
// target is rejected rather than accidentally addressing a replacement object.
struct ScriptFieldTarget {
    uint64_t SessionID = 0;
    uint64_t WorldID = 0;
    uint64_t ObjectID = 0;
    uint64_t ScriptInstanceID = 0;
    bool operator==(const ScriptFieldTarget&) const = default;
};
struct ScriptObjectTarget {
    uint64_t SessionID = 0;
    uint64_t WorldID = 0;
    uint64_t ObjectID = 0;
    bool operator==(const ScriptObjectTarget&) const = default;
};
struct ScriptFieldSnapshot {
    GameSproutRuntime::ScriptFieldInfo Info;
    GameSproutRuntime::ScriptValue Value;
    std::string ObjectName;
    std::string ObjectPath;
    bool MissingObject = false;
    std::vector<ScriptFieldSnapshot> Children; // Structure/sequence children; dictionary uses key,value pairs.
};
struct ScriptPropertySnapshot {
    ScriptFieldTarget Target;
    std::vector<ScriptFieldSnapshot> Fields;
};
struct ScriptObjectChoice {
    uint64_t ObjectFileID = 0; // 0 is the scene root, not an empty reference.
    std::string Name;
    std::string Path;
    // Runtime identity is transient and valid only in the current field session.
    uint64_t RuntimeObjectID = 0;
    uint64_t RuntimeSceneID = 0;
};
// Paths address a value below one declared root field, never a native memory address.
enum class ScriptPathKind : uint8_t { Member, Element, EntryKey, EntryValue };
struct ScriptPathStep {
    ScriptPathKind Kind=ScriptPathKind::Member;
    std::string Member;
    std::size_t Index=0;
    bool operator==(const ScriptPathStep&) const = default;
};
using ScriptFieldPath=std::vector<ScriptPathStep>;
enum class ScriptContainerAction : uint8_t { Resize, Insert, Remove, Move };
struct ScriptContainerEdit {
    ScriptContainerAction Action=ScriptContainerAction::Insert;
    std::size_t Index=0, Destination=0;
};
struct ScriptComponentChoice {
    ScriptObjectTarget Object;
    uint64_t ComponentInstanceID=0;
    std::string Name, Path, Type;
    uint64_t ComponentFileID=0; // Existing identity only; browsing never allocates.
};
class IScriptEditorListener {
public:
    virtual ~IScriptEditorListener() = default;
    virtual void OnScriptTypesChanged(const std::vector<EditorScriptType>& types) {}
    virtual void OnScriptBuildStateChanged(const ScriptBuildState& state) {}
    // Emitted after an explicit edit or a scene/type/reference event, never by
    // idle polling. The receiver may request one fresh field snapshot.
    virtual void OnScriptFieldsChanged(const ScriptFieldTarget& target) {}
};
class IScriptEditor : public GameSproutEditor::IGameSproutModule {
public:
    virtual std::vector<EditorScriptType> GetScriptTypes() const = 0;
    virtual ScriptBuildState GetBuildState() const = 0;
    virtual bool RegisterScriptListener(IScriptEditorListener* listener) = 0;
    virtual void UnregisterScriptListener(IScriptEditorListener* listener) = 0;
    // Creates one .hpp without overwriting any source/.meta. AssetsManager creates its .meta.
    virtual bool CreateScript(const std::string& relativeDirectory, const std::string& name,
                              std::string& relativePath, std::string& error) = 0;
    virtual bool RequestCompile(std::string& error) = 0;
    // No Runtime node pointers or field-access closures cross this boundary.
    // Initial read happens on selection; later reads follow events/user input.
    virtual bool ReadFields(uint64_t objectID, uint64_t scriptInstanceID,
                            ScriptPropertySnapshot& snapshot, std::string& error) const {
        error="脚本字段服务不可用。";return false;
    }
    // One inspected target per listener; replacing it does not accumulate old
    // selection watches. UnregisterScriptListener also releases this watch.
    virtual bool ObserveFields(const ScriptFieldTarget&, IScriptEditorListener*) {return false;}
    virtual void UnobserveFields(IScriptEditorListener*) {}
    virtual bool SetField(const ScriptFieldTarget& target, const std::string& fieldID,
                          const GameSproutRuntime::ScriptValue& value, std::string& error) {
        error="脚本字段服务不可用。";return false;
    }
    virtual bool GetSceneObjectChoices(const ScriptFieldTarget& target,
                                      std::vector<ScriptObjectChoice>& choices, std::string& error) const {
        error="场景对象选择服务不可用。";return false;
    }
    virtual bool ReadObjectTarget(uint64_t objectID, ScriptObjectTarget& target, std::string& error) const {
        error="场景对象身份服务不可用。";return false;
    }
    virtual bool ReferenceFromObject(const ScriptFieldTarget& target, const ScriptObjectTarget& source,
                                     GameSproutRuntime::ScriptValue& value, std::string& error) const {
        error="场景对象引用服务不可用。";return false;
    }
    virtual bool LocateObjectReference(const ScriptFieldTarget& target,
                                       const GameSproutRuntime::ScriptValue& value, std::string& error) {
        error="场景对象定位服务不可用。";return false;
    }
    virtual bool SetFieldPath(const ScriptFieldTarget& target,const std::string& fieldID,
        const ScriptFieldPath& path,const GameSproutRuntime::ScriptValue& value,std::string& error) {
        if(path.empty())return SetField(target,fieldID,value,error);
        error="递归字段编辑服务不可用。";return false;
    }
    virtual bool EditContainer(const ScriptFieldTarget&,const std::string&,const ScriptFieldPath&,
        const ScriptContainerEdit&,std::string& error) {error="容器编辑服务不可用。";return false;}
    virtual bool GetSceneComponentChoices(const ScriptFieldTarget&,const std::string& requiredType,
        std::vector<ScriptComponentChoice>&,std::string& error) const {error="组件选择服务不可用。";return false;}
    virtual bool ReferenceFromComponent(const ScriptFieldTarget&,const ScriptObjectTarget&,uint64_t componentInstanceID,
        const std::string& requiredType,GameSproutRuntime::ScriptValue&,std::string& error) const {
        error="组件引用服务不可用。";return false;
    }
    // Object drops resolve exactly one compatible component; zero/multiple matches are rejected.
    virtual bool ComponentReferenceFromObject(const ScriptFieldTarget&,const ScriptObjectTarget&,
        const std::string& requiredType,GameSproutRuntime::ScriptValue&,std::string& error) const {
        error="组件引用服务不可用。";return false;
    }
    virtual bool LocateComponentReference(const ScriptFieldTarget&,const GameSproutRuntime::ScriptValue&,std::string& error) {
        error="组件定位服务不可用。";return false;
    }
    // Freeze this successful native module for an execution session. Changes
    // received while blocked compile after the session ends, without replacing
    // code referenced by live script instances.
    virtual bool SetReloadBlocked(bool blocked, std::string& error) { error.clear(); return true; }
};

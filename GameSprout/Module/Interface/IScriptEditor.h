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
};
struct ScriptPropertySnapshot {
    ScriptFieldTarget Target;
    std::vector<ScriptFieldSnapshot> Fields;
};
struct ScriptObjectChoice {
    uint64_t ObjectFileID = 0; // 0 is the scene root, not an empty reference.
    std::string Name;
    std::string Path;
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
    // Freeze this successful native module for an execution session. Changes
    // received while blocked compile after the session ends, without replacing
    // code referenced by live script instances.
    virtual bool SetReloadBlocked(bool blocked, std::string& error) { error.clear(); return true; }
};

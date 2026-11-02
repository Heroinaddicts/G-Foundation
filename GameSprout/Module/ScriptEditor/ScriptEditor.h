#pragma once
#include "IScriptEditor.h"
#include "IScriptCompiler.h"
#include "IAssetsManager.h"
#include "IProject.h"
#include "IGuidCreator.h"
#include "ITaskApi.h"
#include "IIDEWindow.h"
#include "IConsole.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include <QObject>
#include <QLibrary>
#include <QLockFile>
#include <map>
#include <set>

class ScriptEditor final : public IScriptEditor, public IAssetListener,
                           public ISceneListener, public GameSproutRuntime::IRuntimeListener {
public:
    std::vector<EditorScriptType> GetScriptTypes() const override { return _Types; }
    ScriptBuildState GetBuildState() const override;
    bool RegisterScriptListener(IScriptEditorListener*) override;
    void UnregisterScriptListener(IScriptEditorListener*) override;
    bool CreateScript(const std::string&,const std::string&,std::string&,std::string&) override;
    bool RequestCompile(std::string&) override;
    bool SetReloadBlocked(bool,std::string&) override;
    bool ReadFields(uint64_t,uint64_t,ScriptPropertySnapshot&,std::string&) const override;
    bool ObserveFields(const ScriptFieldTarget&,IScriptEditorListener*) override;
    void UnobserveFields(IScriptEditorListener*) override;
    bool SetField(const ScriptFieldTarget&,const std::string&,const GameSproutRuntime::ScriptValue&,std::string&) override;
    bool GetSceneObjectChoices(const ScriptFieldTarget&,std::vector<ScriptObjectChoice>&,std::string&) const override;
    bool ReadObjectTarget(uint64_t,ScriptObjectTarget&,std::string&) const override;
    bool ReferenceFromObject(const ScriptFieldTarget&,const ScriptObjectTarget&,GameSproutRuntime::ScriptValue&,std::string&) const override;
    bool LocateObjectReference(const ScriptFieldTarget&,const GameSproutRuntime::ScriptValue&,std::string&) override;
    void OnActiveSceneChanged() override;
    void OnSceneStateChanged() override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override;
    void OnAssetCreate(const AssetEvent&) override;
    void OnAssetModify(const AssetEvent&) override;
    void OnAssetRemove(const AssetEvent&) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override;
    void LaunchFinished(GameSproutEditor::GameSproutModuleApi*) override;
    void Release(GameSproutEditor::GameSproutModuleApi*) override;
    void SourceEvent(const AssetEvent&,bool remove);
    void QueueCompile();
    void Compile();
    bool PrepareAssembly(std::string& error);
    bool Load(const ScriptCompileRequest&,const ScriptCompileResult&,std::string&);
    QLibrary* CurrentLibrary() const {return _CurrentSlot>=0?_SlotLibraries[_CurrentSlot]:nullptr;}
    bool PrepareSlots(const std::string& buildRoot,std::string& error);
    bool RetireSlot(int slot,std::string& error);
    void ReleaseSlotLock();
    QString SlotDirectory(int slot) const;
    void Notify(bool types);
    void Log(const std::string&,bool error);
    GameSproutRuntime::IScriptComponent* ResolveFieldTarget(const ScriptFieldTarget&,std::string&) const;
    void NotifyFields();
    IScriptCompiler* _Compiler = nullptr;
    IAssetsManager* _Assets = nullptr;
    IProject* _Project = nullptr;
    IGuidCreator* _Guids = nullptr;
    IConsole* _Console = nullptr;
    IIDEWindow* _IDE = nullptr;
    Foundation::ITaskApi* _Tasks = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime = nullptr;
    ISceneManager* _Scenes = nullptr;
    ISelectionManager* _Selection = nullptr;
    bool _FieldSceneSubscribed = false, _FieldRuntimeSubscribed = false;
    uint64_t _FieldSessionID = 1;
    std::map<IScriptEditorListener*,ScriptFieldTarget> _FieldObservers;
    std::map<std::string,ScriptCompileSource> _Sources;
    std::map<uint64_t,IScriptEditorListener*> _Listeners;
    std::vector<EditorScriptType> _Types;
    std::vector<GameSproutRuntime::ScriptTypeRegistration> _Registrations;
    // Both active and unsuccessfully retired modules retain their handles.
    QLibrary* _SlotLibraries[2] = {nullptr,nullptr};
    bool _SlotEverLoaded[2] = {false,false};
    int _CurrentSlot = -1;
    QLockFile* _SlotsLock = nullptr;
    std::string _SlotsRoot;
    QObject* _Dispatch = nullptr;
    UInt64 _Owner = Foundation::InvalidTaskOwnerId;
    UInt64 _Task = Foundation::InvalidTaskId;
    uint64_t _ListenerID = 0, _Revision = 0, _PublishedRevision = 0;
    bool _Launched = false, _Queued = false, _Loading = false, _Subscribed = false;
    bool _ReloadBlocked = false, _CompileDeferred = false;
    ScriptBuildState _State;
    std::string _LoadedFingerprint;
    std::string _LoadedLibraryHash;
    std::string _AssemblyGuid;
};

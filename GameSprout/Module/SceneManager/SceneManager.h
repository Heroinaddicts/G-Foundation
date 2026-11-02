#pragma once
#include "ISceneManager.h"
#include "IProject.h"
#include "SceneDocument.h"
#include <map>

class SceneManager final : public ISceneManager, public GameSproutRuntime::IRuntimeListener {
public:
    ~SceneManager() override;
    GameSproutRuntime::IScene* GetActiveScene() const override;
    const std::string& GetActiveScenePath() const override { return _ActivePath; }
    bool OpenScene(const std::string& path, std::string& error) override;
    bool BeginPlaySession(std::string& error) override;
    bool EndPlaySession(std::string& error) override;
    bool IsPlaySessionActive() const override {return _PlaySession!=nullptr;}
    GameSproutRuntime::IGameObject* InstantiateModel(const GameSproutRuntime::ResourceReference& model,
        uint64_t parentID, std::string& error) override;
    bool EditObject(uint64_t objectID,const std::string& label,const fSceneEditOperation& apply,
                    const fSceneEditOperation& revert,std::string& error) override;
    bool DeleteObject(uint64_t objectID,std::string& error) override;
    bool AddBuiltinComponent(uint64_t objectID,const std::string& typeName,std::string& error) override;
    bool RemoveComponent(uint64_t objectID,const std::string& typeName,std::string& error) override;
    bool AttachScriptComponent(uint64_t objectID,const std::string& typeID,
        const GameSproutRuntime::ResourceReference& source,std::string& error) override;
    bool RemoveScriptComponent(uint64_t objectID,uint64_t instanceID,std::string& error) override;
    uint64_t GetObjectFileID(uint64_t objectID) const override;
    GameSproutRuntime::IGameObject* QueryObjectByFileID(uint64_t fileID) const override;
    bool BeginTransformEdit(uint64_t objectID,const std::string& label,std::string& error) override;
    bool PreviewTransformEdit(const SceneTransformState& state,std::string& error) override;
    bool CommitTransformEdit(std::string& error) override;
    bool CancelTransformEdit(std::string& error) override;
    bool HasTransformEdit() const override {return _TransformEdit.Active;}
    bool Undo(std::string& error) override;
    bool Redo(std::string& error) override;
    bool CanUndo() const override {return !IsPlaySessionActive()&&_HistoryPosition>0;}
    bool CanRedo() const override {return !IsPlaySessionActive()&&_HistoryPosition<_History.size();}
    bool SaveScene(std::string& error) override;
    bool SaveSceneAs(const std::string& relativePath,std::string& error) override;
    bool HasUnsavedChanges() const override;
    bool CreateSceneAsset(const std::string& directory, const std::string& name, std::string& path, std::string& error) override;
    bool RegisterSceneListener(ISceneListener* listener) override;
    void UnregisterSceneListener(ISceneListener* listener) override;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override;
private:
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) override;
    bool Initialize(GameSproutEditor::GameSproutModuleApi* api) override;
    bool Launch(GameSproutEditor::GameSproutModuleApi* api) override;
    void Update(GameSproutEditor::GameSproutModuleApi* api) override;
    void Release(GameSproutEditor::GameSproutModuleApi* api) override;
    void NotifySceneChanged();
    void NotifyStateChanged();
    void NotifyListeners(bool active);
    void NotifyPlaySession(bool changing,bool active);
    void Activate(GameSproutRuntime::IScene* scene,std::string path,SceneDocument::ObjectIDs ids,std::string diskHash={});
    bool RestoreLastScene();
    void RememberActiveScene() const;
    GameSproutRuntime::IGameSproutRuntime* Runtime() const;
    bool Ready(std::string& error) const;
    bool Capture(Json::Value& document,std::string& text,std::string& error) const;
    void RefreshState(bool force=false);
    bool SaveTo(const std::string& path,bool allowNew,std::string& error);
    struct Edit {
        enum class Kind {Property,Deletion,Creation,Component};
        Kind Type=Kind::Property;
        std::uint64_t FileID=0;
        std::string Label;
        fSceneEditOperation Apply;
        fSceneEditOperation Revert;
        Json::Value Subtree;
        Json::Value AnimationBindings;
        uint64_t ParentFileID=0;
        std::size_t SiblingIndex=0;
        Json::Value ComponentBefore,ComponentAfter;
        std::size_t ComponentIndex=0;
        bool ComponentWasMainCamera=false;
    };
    struct PlaySession {
        struct SceneSnapshot {Json::Value Document;SceneDocument::ObjectIDs IDs;};
        std::vector<SceneSnapshot> Scenes;
        std::size_t ActiveIndex=0;
        std::string Path,BaselineText,DiskHash,LastStateText;
        std::vector<Edit> History;
        std::size_t HistoryPosition=0;
        bool Unsaved=false;
    };
    PlaySession* _PlaySession=nullptr;
    bool RunEdit(const Edit& edit,bool forward,std::string& error);
    bool CaptureDeletion(GameSproutRuntime::IGameObject& object,Edit& edit,std::string& error) const;
    bool RunDeletion(const Edit& edit,bool forward,std::string& error);
    bool RunComponentEdit(const Edit& edit,bool forward,std::string& error);
    bool ChangeComponent(uint64_t objectID,const std::string& typeName,uint64_t scriptInstanceID,
        const std::string& scriptTypeID,const GameSproutRuntime::ResourceReference& source,
        bool adding,std::string& error);
    GameSproutRuntime::IGameObject* TransformTarget(bool checkParent) const;
    struct TransformEdit {
        bool Active=false;
        uint64_t SceneID=0,ObjectID=0,ParentID=0;
        std::string Label;
        SceneTransformState Before,Last;
        GameSproutRuntime::Mat4 ParentWorld;
    } _TransformEdit;
    IProject* _Project = nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    std::uint64_t _ActiveID = 0;
    std::string _ActivePath;
    bool _RuntimeSubscribed = false;
    bool _Switching = false;
    bool _Notifying = false;
    bool _NotifyAgain = false;
    bool _StateNotifyAgain = false;
    bool _ExecutingEdit = false;
    bool _StatePending = false;
    mutable bool _Unsaved = true;
    mutable SceneDocument::ObjectIDs _ObjectIDs;
    std::string _BaselineText;
    std::string _DiskHash; // 打开/上次保存的原始文件字节 SHA256，不能用规范化快照代替。
    std::string _LastStateText;
    std::vector<Edit> _History;
    std::size_t _HistoryPosition = 0;
    std::uint64_t _NextListener = 0;
    std::map<std::uint64_t,ISceneListener*> _Listeners;
};

#pragma once
#include "ISceneManager.h"
#include "IProject.h"
#include "SceneDocument.h"
#include "SceneDirtyState.h"
#include "ITaskApi.h"
#include <map>
#include <array>
class IRuntimeHost;
class IIDEWindow;
class QString;

class SceneManager final : public ISceneManager, public GameSproutRuntime::IRuntimeListener {
public:
    ~SceneManager() override;
    GameSproutRuntime::IScene* GetActiveScene() const override;
    const std::string& GetActiveScenePath() const override { return _ActivePath; }
    std::string GetMainScenePath() const override;
    bool BeginDocumentSession(const SceneDocumentSessionRequest&,std::string&) override;
    bool EndDocumentSession(std::string&) override;
    bool IsDocumentSessionActive() const override {return _DocumentSession!=nullptr;}
    bool IsEditingSuspendedDocument() const override {return _EditingSuspended;}
    GameSproutRuntime::IGameObject* GetDocumentContentRoot() const override;
    GameSproutRuntime::IScene* GetMainScene() const override;
    bool MarkDocumentSessionSaved(std::string&) override;
    bool EditSuspendedDocument(const std::function<bool(std::string&)>&,std::string&) override;
    bool OpenScene(const std::string& path, std::string& error) override;
    bool OpenSceneAsync(const std::string& path,fSceneOpenCompleted completed,std::string& error) override;
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
    bool RemoveComponentInstance(uint64_t objectID,uint64_t componentInstanceID,std::string& error) override;
    uint64_t GetComponentFileID(uint64_t objectID,uint64_t componentInstanceID) const override;
    GameSproutRuntime::IComponent* QueryComponentByFileID(uint64_t objectID,uint64_t componentFileID) const override;
    bool AttachScriptComponent(uint64_t objectID,const std::string& typeID,
        const GameSproutRuntime::ResourceReference& source,std::string& error) override;
    bool RemoveScriptComponent(uint64_t objectID,uint64_t instanceID,std::string& error) override;
    uint64_t GetObjectFileID(uint64_t objectID) const override;
    GameSproutRuntime::IGameObject* QueryObjectByFileID(uint64_t fileID) const override;
    bool CaptureObjectDocument(uint64_t objectID,SceneSubtreeDocument& output,std::string& error) const override;
    std::string GetSceneAnnotationsJson() const override;
    bool EditObjectDocument(const SceneDocumentEditRequest& request,SceneSubtreeDocument& output,std::string& error) override;
    bool BeginTransformEdit(uint64_t objectID,const std::string& label,std::string& error) override;
    bool PreviewTransformEdit(const SceneTransformState& state,std::string& error) override;
    bool CommitTransformEdit(std::string& error) override;
    bool CancelTransformEdit(std::string& error) override;
    bool HasTransformEdit() const override {return _TransformEdit.Active;}
    bool BeginPropertyPreview(std::string&) override;
    void EndPropertyPreview() override;
    void PublishPropertyPreview() override;
    bool ValidatePropertyPreview(std::string&) const override;
    bool HasPropertyPreview() const override {return _PropertyPreview;}
    bool SetObjectLayer(uint64_t,std::uint8_t,bool,std::string&) override;
    bool SetObjectStaticFlags(uint64_t,GameSproutRuntime::StaticFlags,GameSproutRuntime::StaticFlags,bool,std::string&) override;
    std::vector<std::uint64_t> GetReferencedTagIDs() const override;
    bool Undo(std::string& error) override;
    bool Redo(std::string& error) override;
    bool CanUndo() const override {return !IsPlaySessionActive()&&_HistoryPosition>0;}
    bool CanRedo() const override {return !IsPlaySessionActive()&&_HistoryPosition<_History.size();}
    bool SaveScene(std::string& error) override;
    bool SaveSceneAs(const std::string& relativePath,std::string& error) override;
    bool HasUnsavedChanges() const override;
    bool HasCachedUnsavedChanges() const override {return _Unsaved;}
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
    void Activate(GameSproutRuntime::IScene* scene,std::string path,SceneDocument::ObjectIDs ids,std::string diskHash={},Json::Value annotations=Json::Value(Json::objectValue),SceneDirtyState baseline={});
    bool CreateDefaultScene(std::string& error);
    bool RestoreLastScene();
    void RememberActiveScene() const;
    GameSproutRuntime::IGameSproutRuntime* Runtime() const;
    bool Ready(std::string& error) const;
    bool Capture(Json::Value& document,std::string& error) const;
    SceneDirtyState MakeDirtyBaseline(const Json::Value&) const;
    bool InitializeOpenSceneBaseline();
    bool SynchronizeDirtyState(std::string& error) const;
    bool RefreshDirtyDocument(const Json::Value&,std::string& error) const;
    bool RefreshDirtyRoot(std::string& error) const;
    bool PublishNewIdentities(const std::vector<GameSproutRuntime::IGameObject*>&,std::string& error) const;
    bool CaptureSubtree(GameSproutRuntime::IGameObject&,Json::Value&,std::string& error,bool validateDocument=true) const;
    bool DirtyFromSnapshot() const;
    void RefreshState(bool force=false);
    bool SaveTo(const std::string& path,bool allowNew,std::string& error);
    bool CanOpenScene(std::string& error) const;
    bool ResolveSceneFile(const std::string& path,QString& source,std::string& relative,std::string& error) const;
    struct OpenRequest;
    void AdvanceOpenScene();
    void SubmitOpenSceneBaseline();
    void FinishOpenScene(bool success,std::string error);
    void CancelOpenScene();
    OpenRequest* _OpenRequest=nullptr;
    Foundation::ITaskApi* _Tasks=nullptr;
    UInt64 _TaskOwner=Foundation::InvalidTaskOwnerId;
    IIDEWindow* _IDE=nullptr;
    struct Edit {
        enum class Kind {Property,Deletion,Creation,Component,Document};
        Kind Type=Kind::Property;
        std::uint64_t FileID=0;
        std::string Label;
        std::array<std::uint64_t,2> ReferencedTags{};
        fSceneEditOperation Apply;
        fSceneEditOperation Revert;
        Json::Value Subtree;
        Json::Value AnimationBindings;
        uint64_t ParentFileID=0;
        std::size_t SiblingIndex=0;
        Json::Value ComponentBefore,ComponentAfter;
        std::size_t ComponentIndex=0;
        bool ComponentWasMainCamera=false;
        Json::Value DocumentBefore,DocumentAfter,AnnotationsBefore,AnnotationsAfter;
    };
    struct DocumentContext {
        uint64_t ActiveID=0;
        SceneDocument::ObjectIDs IDs;
        std::string Path,DiskHash;
        std::vector<Edit> History;
        std::size_t HistoryPosition=0;
        Json::Value Annotations{Json::objectValue};
        bool Unsaved=true,StatePending=false;
        SceneDirtyState DirtyState;
    };
    struct PlaySession {
        DocumentContext Author;
        std::set<std::uint64_t> SceneTags;
    };
    PlaySession* _PlaySession=nullptr;
    struct DocumentSession {DocumentContext Main;uint64_t ContentRootFileID=0;bool MainSimulationEnabled=true;};
    DocumentSession* _DocumentSession=nullptr;
    bool _EditingSuspended=false;
    void SwapDocumentContext(DocumentContext&) noexcept;
    bool ValidateDocumentContent(const Json::Value&,std::string&) const;
    bool IsTemporaryDocumentContext() const {return _DocumentSession&&!_EditingSuspended;}
    bool RunEdit(const Edit& edit,bool forward,std::string& error);
    bool CaptureDeletion(GameSproutRuntime::IGameObject& object,Edit& edit,std::string& error) const;
    bool RunDeletion(const Edit& edit,bool forward,std::string& error,bool firstExecution=false);
    bool RunComponentEdit(const Edit& edit,bool forward,std::string& error);
    bool RunDocumentEdit(const Edit& edit,bool forward,std::string& error);
    bool InsertObjectDocument(const SceneDocumentEditRequest&,Json::Value&,uint64_t,SceneSubtreeDocument&,std::string&);
    bool ApplyDocument(const Json::Value& before,const Json::Value& after,std::string& error);
    bool ChangeComponent(uint64_t objectID,const std::string& typeName,uint64_t scriptInstanceID,
        const std::string& scriptTypeID,const GameSproutRuntime::ResourceReference& source,
        bool adding,std::string& error,uint64_t componentInstanceID=0);
    GameSproutRuntime::IGameObject* TransformTarget(bool checkParent) const;
    struct TransformEdit {
        bool Active=false;
        uint64_t SceneID=0,ObjectID=0,ParentID=0;
        std::string Label;
        SceneTransformState Before,Last;
        GameSproutRuntime::Mat4 ParentWorld;
    } _TransformEdit;
    IProject* _Project = nullptr;
    IRuntimeHost* _Host=nullptr;
    GameSproutRuntime::IGameSproutRuntime* _Runtime=nullptr; // Borrowed from EditorApi.
    std::uint64_t _ActiveID = 0;
    std::string _ActivePath;
    bool _RuntimeSubscribed = false;
    bool _Switching = false;
    bool _Notifying = false;
    bool _NotifyAgain = false;
    bool _StateNotifyAgain = false;
    bool _ExecutingEdit = false;
    bool _PropertyPreview = false;
    bool _StatePending = false;
    mutable bool _Unsaved = true;
    mutable SceneDirtyState _DirtyState;
    mutable SceneDocument::ObjectIDs _ObjectIDs;
    Json::Value _Annotations{Json::objectValue};
    std::string _DiskHash; // 打开/上次保存的原始文件字节 SHA256，不能用规范化快照代替。
    std::vector<Edit> _History;
    std::size_t _HistoryPosition = 0;
    std::uint64_t _NextListener = 0;
    std::map<std::uint64_t,ISceneListener*> _Listeners;
};

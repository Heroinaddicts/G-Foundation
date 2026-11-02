#pragma once
#include "GameSproutEditorApi.h"
#include "GameSproutRuntimeApi.h"

class ISceneListener {
public:
    virtual ~ISceneListener() = default;
    // 主线程同步通知，借用当前场景；切换时不要继续使用上一个场景的指针。
    virtual void OnActiveSceneChanged() = 0;
    // 属性修改、撤销/重做或保存状态变化，不代表切换场景，不应清空选择。
    virtual void OnSceneStateChanged() {}
    // 候选世界已经验证；Changing 回调仍可读取旧世界，Changed 回调读取新世界。
    // 回调期间禁止场景事务重入；不要保留旧节点指针。
    virtual void OnPlaySessionChanging(bool entering) {}
    virtual void OnPlaySessionChanged(bool active) {}
};

struct SceneTransformState {
    GameSproutRuntime::Vector3 Position;
    GameSproutRuntime::Quaternion Rotation;
    GameSproutRuntime::Vector3 Scale{1,1,1};
};

// 文档身份均为值；组件的 RuntimeObjectID 是其所属节点身份。
struct SceneDocumentObjectID {
    uint64_t DocumentFileID=0;
    uint64_t SceneFileID=0;
    uint64_t RuntimeObjectID=0;
    bool Component=false;
};
struct SceneSubtreeDocument {
    uint64_t RootObjectID=0;
    std::string DocumentJson;
    std::vector<SceneDocumentObjectID> ObjectIDs;
    std::vector<uint64_t> ExternalObjectFileIDs;
};
struct SceneDocumentEditRequest {
    // RootObjectID=0 插入；ParentObjectID=0 表示场景根。替换时 ParentObjectID
    // 为 0 保持原父级。空 DocumentJson 只修改场景 annotations。
    uint64_t RootObjectID=0;
    uint64_t ParentObjectID=0;
    std::string DocumentJson;
    // 空字符串保持原 annotations；非空必须是 JSON object。
    std::string AnnotationsJson;
    std::string Label;
    // 插入来自独立文档的子树时为对象和组件分配新场景 FileID，重写内部引用。
    bool RemapDocumentIDs=false;
    // 只允许纯值转换，不能查询/修改 Runtime 或重入场景服务。候选身份映射准备
    // 完成后调用一次；与节点变更作为同一 Undo/Redo 事务提交。
    std::function<bool(const SceneSubtreeDocument&,const std::string&,std::string&,std::string&)> BuildAnnotations;
};

// Generic, single-level authoring document. The optional content root is a
// protected ordinary node; the synthetic Runtime scene remains outside it.
struct SceneDocumentSessionRequest {
    std::string DocumentJson,Name;
    uint64_t ContentRootFileID=0;
};

// 编辑器场景服务；运行对象由独立 Runtime 持有，View 只读取/操作公共接口。
class ISceneManager : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~ISceneManager() = default;
    virtual GameSproutRuntime::IScene* GetActiveScene() const = 0;
    // 相对项目 Assets 的路径，未保存的默认场景返回空字符串。
    virtual const std::string& GetActiveScenePath() const = 0;
    // Main authoring source remains open while a temporary Prefab document is active.
    virtual std::string GetMainScenePath() const {return GetActiveScenePath();}
    virtual bool BeginDocumentSession(const SceneDocumentSessionRequest&,std::string& error) {error="当前场景服务不支持临时文档。";return false;}
    virtual bool EndDocumentSession(std::string& error) {error.clear();return true;}
    virtual bool IsDocumentSessionActive() const {return false;}
    virtual bool IsEditingSuspendedDocument() const {return false;}
    virtual GameSproutRuntime::IGameObject* GetDocumentContentRoot() const {return GetActiveScene();}
    virtual GameSproutRuntime::IScene* GetMainScene() const {return GetActiveScene();}
    virtual bool MarkDocumentSessionSaved(std::string& error) {error="当前没有临时文档。";return false;}
    // Host safe point only. Temporarily addresses the suspended main authoring
    // document through public APIs without publishing a false scene switch.
    virtual bool EditSuspendedDocument(const std::function<bool(std::string&)>&,std::string& error) {error="当前没有挂起的主文档。";return false;}
    virtual bool OpenScene(const std::string& relativePath, std::string& error) = 0;
    // 主线程、Runtime 回调之外、脚本执行关闭时调用。捕获完整内存编辑会话，
    // 运行使用重建的临时世界；停止恢复未保存内容、文件身份与编辑历史，不写 .scene。
    virtual bool BeginPlaySession(std::string& error) {error="场景服务不支持运行会话。";return false;}
    virtual bool EndPlaySession(std::string& error) {error="场景服务不支持运行会话。";return false;}
    virtual bool IsPlaySessionActive() const {return false;}
    // Model instantiation is an editor transaction; View never reads FBX or builds bones.
    virtual GameSproutRuntime::IGameObject* InstantiateModel(const GameSproutRuntime::ResourceReference&,
        uint64_t, std::string& error) {error="当前场景服务不支持模型实例化。";return nullptr;}
    using fSceneEditOperation = std::function<bool(GameSproutRuntime::IGameObject&,std::string&)>;
    // 闭包仅捕获值。执行前按 ID 重新查询当前场景对象，不应捕获 UI/Runtime 对象指针。
    // 主线程同步执行；Runtime 更新/遍历/事件回调及场景操作重入时拒绝。
    virtual bool EditObject(uint64_t objectID, const std::string& label,
                            const fSceneEditOperation& apply, const fSceneEditOperation& revert,
                            std::string& error) = 0;
    // Authoring transaction; includes descendants only when explicitly requested.
    // A whole subtree change is one Undo/Redo step. View does not edit document JSON.
    virtual bool SetObjectLayer(uint64_t objectID,std::uint8_t layer,bool includeChildren,std::string& error) {
        error="当前场景服务不支持 Layer 编辑。";return false;
    }
    // Generic authoring metadata transaction. Only changedFlags bits are updated;
    // descendants preserve their own unrelated flags and share one Undo/Redo step.
    virtual bool SetObjectStaticFlags(uint64_t objectID,GameSproutRuntime::StaticFlags values,
        GameSproutRuntime::StaticFlags changedFlags,bool includeChildren,std::string& error) {
        error="当前场景服务不支持静态标志编辑。";return false;
    }
    // Explicit project-definition deletion checks include parked author history.
    virtual std::vector<std::uint64_t> GetReferencedTagIDs() const {return {};}
    // 删除普通节点及完整子树；Undo仅重建被删子树，Runtime身份会改变。
    virtual bool DeleteObject(uint64_t, std::string& error) {error="当前场景服务不支持删除节点。";return false;}
    // 结构变更由场景事务持有；不销毁对象或其余组件，支持保存和撤销/重做。
    // typeName 使用 Runtime 公布的稳定类型名；Transform 固有且不可移除。
    virtual bool AddBuiltinComponent(uint64_t, const std::string&, std::string& error) {error="当前场景服务不支持添加组件。";return false;}
    virtual bool RemoveComponent(uint64_t, const std::string&, std::string& error) {error="当前场景服务不支持移除组件。";return false;}
    // 精确移除当前 Runtime 组件；重复类型必须用实例身份，Undo 保留文档组件 ID。
    virtual bool RemoveComponentInstance(uint64_t,uint64_t,std::string& error) {error="当前场景服务不支持按实例移除组件。";return false;}
    virtual uint64_t GetComponentFileID(uint64_t,uint64_t) const {return 0;}
    virtual GameSproutRuntime::IComponent* QueryComponentByFileID(uint64_t,uint64_t) const {return nullptr;}
    virtual bool AttachScriptComponent(uint64_t, const std::string&, const GameSproutRuntime::ResourceReference&,
                                       std::string& error) {error="当前场景服务不支持挂载脚本。";return false;}
    // instanceID 为脚本挂载实例身份；同一对象可以挂载多个不同实例。
    virtual bool RemoveScriptComponent(uint64_t, uint64_t, std::string& error) {error="当前场景服务不支持移除脚本。";return false;}
    // 稳定源文件身份，0表示当前场景根；无效Runtime身份同样返回0，调用方应先验证对象。
    virtual uint64_t GetObjectFileID(uint64_t) const {return 0;}
    virtual GameSproutRuntime::IGameObject* QueryObjectByFileID(uint64_t) const {return nullptr;}
    // 捕获指定普通节点的完整标准子树文档，根 ParentID=0；包含稳定组件 ID 和外部对象引用。
    virtual bool CaptureObjectDocument(uint64_t,SceneSubtreeDocument&,std::string& error) const {error="当前场景服务不支持子树文档。";return false;}
    virtual std::string GetSceneAnnotationsJson() const {return "{}";}
    // 通用文档事务，不解释 annotations 的业务内容；Play/回调/其他编辑事务期间拒绝。
    // 替换尽量保留 FileID 相同的节点与组件身份；失败回滚，成功只追加一条历史。
    virtual bool EditObjectDocument(const SceneDocumentEditRequest&,SceneSubtreeDocument&,std::string& error) {error="当前场景服务不支持文档事务。";return false;}
    // Scene 拖拽：Preview 实时修改同一 Runtime 对象，不追加历史；Commit 一拖一条，Cancel 不截断 Redo。
    // 活动事务期间拒绝其他属性编辑/Undo/Redo/保存/打开场景。全部在宿主主线程调用。
    // Preview先验证完整TRS及场景可序列化性，失败保留上次有效预览；同步发StateChanged。
    // 目标删除/重挂父级/父世界变换变化会自动取消，恢复存活目标的原局部TRS。
    virtual bool BeginTransformEdit(uint64_t,const std::string&,std::string& error) {error="当前场景服务不支持连续变换编辑。";return false;}
    virtual bool PreviewTransformEdit(const SceneTransformState&,std::string& error) {error="没有连续变换编辑。";return false;}
    virtual bool CommitTransformEdit(std::string& error) {error="没有连续变换编辑。";return false;}
    virtual bool CancelTransformEdit(std::string& error) {error.clear();return true;}
    virtual bool HasTransformEdit() const {return false;}
    virtual bool Undo(std::string& error) = 0;
    virtual bool Redo(std::string& error) = 0;
    virtual bool CanUndo() const = 0;
    virtual bool CanRedo() const = 0;
    virtual bool SaveScene(std::string& error) = 0;
    // 相对 Assets；首次保存可创建新文件，不静默覆盖另一个已存在场景。
    virtual bool SaveSceneAs(const std::string& relativePath, std::string& error) = 0;
    virtual bool HasUnsavedChanges() const = 0;
    // 在已存在的 Assets 子目录中创建，不覆盖已有文件；outPath 同样相对 Assets。
    virtual bool CreateSceneAsset(const std::string& directory, const std::string& name,
                                  std::string& outPath, std::string& error) = 0;
    // 主线程，先订阅再查询；不回放历史，不持有监听者，销毁前必须注销。
    virtual bool RegisterSceneListener(ISceneListener* listener) = 0;
    virtual void UnregisterSceneListener(ISceneListener* listener) = 0;
};

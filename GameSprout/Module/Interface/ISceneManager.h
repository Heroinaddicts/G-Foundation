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

// 编辑器场景服务；运行对象由独立 Runtime 持有，View 只读取/操作公共接口。
class ISceneManager : public GameSproutEditor::IGameSproutModule {
public:
    virtual ~ISceneManager() = default;
    virtual GameSproutRuntime::IScene* GetActiveScene() const = 0;
    // 相对项目 Assets 的路径，未保存的默认场景返回空字符串。
    virtual const std::string& GetActiveScenePath() const = 0;
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
    // 删除普通节点及完整子树；Undo仅重建被删子树，Runtime身份会改变。
    virtual bool DeleteObject(uint64_t, std::string& error) {error="当前场景服务不支持删除节点。";return false;}
    // 结构变更由场景事务持有；不销毁对象或其余组件，支持保存和撤销/重做。
    // typeName 使用 Runtime 公布的稳定类型名；Transform 固有且不可移除。
    virtual bool AddBuiltinComponent(uint64_t, const std::string&, std::string& error) {error="当前场景服务不支持添加组件。";return false;}
    virtual bool RemoveComponent(uint64_t, const std::string&, std::string& error) {error="当前场景服务不支持移除组件。";return false;}
    virtual bool AttachScriptComponent(uint64_t, const std::string&, const GameSproutRuntime::ResourceReference&,
                                       std::string& error) {error="当前场景服务不支持挂载脚本。";return false;}
    // instanceID 为脚本挂载实例身份；同一对象可以挂载多个不同实例。
    virtual bool RemoveScriptComponent(uint64_t, uint64_t, std::string& error) {error="当前场景服务不支持移除脚本。";return false;}
    // 稳定源文件身份，0表示当前场景根；无效Runtime身份同样返回0，调用方应先验证对象。
    virtual uint64_t GetObjectFileID(uint64_t) const {return 0;}
    virtual GameSproutRuntime::IGameObject* QueryObjectByFileID(uint64_t) const {return nullptr;}
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

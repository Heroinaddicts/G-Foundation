#pragma once

#include "GameSproutRuntimeApi.h"
#include "JsonCpp/json.hpp"
#include <string>
#include <map>

namespace SceneDocument {
struct ObjectIDs {
    std::map<std::uint64_t,std::uint64_t> RuntimeToFile;
    std::uint64_t Next = 1; // 0 表示 uint64 文件 ID 空间耗尽，不能再分配。
    // Component InstanceID is unique for its owner's lifetime, even when the
    // allocator reuses a component address.
    std::uint64_t FindComponentFileID(const GameSproutRuntime::IComponent* component) const {
        if(!component||!component->GetGameObject())return 0;
        const auto identity=_ComponentToFile.find({component->GetGameObject()->GetInstanceID(),component->GetComponentInstanceID()});
        return identity==_ComponentToFile.end()?0:identity->second;
    }
    void BindComponentFileID(const GameSproutRuntime::IComponent* component,std::uint64_t id) {
        if(!component||!component->GetGameObject()||!id)return;
        _ComponentToFile[{component->GetGameObject()->GetInstanceID(),component->GetComponentInstanceID()}]=id;
    }
    void ForgetComponentFileID(const GameSproutRuntime::IComponent* component) {
        if(component&&component->GetGameObject())ForgetComponentFileID(component->GetGameObject()->GetInstanceID(),component->GetComponentInstanceID());
    }
    void ForgetComponentFileID(std::uint64_t owner,std::uint64_t instance) {_ComponentToFile.erase({owner,instance});}
    void PruneComponentFileIDs(const GameSproutRuntime::IGameObject& owner);
    void ForgetObjectFileID(std::uint64_t owner);
    void SwapComponentIdentities(ObjectIDs& other) noexcept {
        _ComponentToFile.swap(other._ComponentToFile);
    }
    void MergeNewIdentities(ObjectIDs& added) {
        RuntimeToFile.merge(added.RuntimeToFile);_ComponentToFile.merge(added._ComponentToFile);Next=added.Next;
    }
private:
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::uint64_t> _ComponentToFile;
};
// 当前 Unity 坐标编辑器源场景。只生成数据，不做文件 IO，也不创建预处理产物。
Json::Value CreateDefault(const std::string& name);

// 宿主线程调用，在 Runtime 更新/遍历/事件回调之外执行。
// 先校验完整文档再创建候选场景；失败回收候选，output 保留原值。
// 文件 ID 与 Runtime InstanceID 分离，ParentID=0 表示场景根。
bool Instantiate(const Json::Value& document, GameSproutRuntime::IGameSproutRuntime& runtime,
                 GameSproutRuntime::IScene*& output, std::string& error);
bool Instantiate(const Json::Value& document, GameSproutRuntime::IGameSproutRuntime& runtime,
                 GameSproutRuntime::IScene*& output, ObjectIDs& ids, std::string& error);
// 输出按深度优先顺序写入。成功后提交新分配的稳定文件 ID，失败保留 ids/output。
bool Serialize(GameSproutRuntime::IScene& scene, ObjectIDs& ids,
               Json::Value& output, std::string& error);
// Existing identities only. No tree walk, ID allocation or binding publication.
// A scene root returns header fields; an ordinary object returns one node record.
bool SerializeProperties(GameSproutRuntime::IGameObject&,const ObjectIDs&,Json::Value&,std::string& error);
// Local component/value validation. Cross-object graph changes still require Validate.
bool ValidateProperties(const Json::Value&,bool sceneRoot,std::string& error);
bool Validate(const Json::Value& document, std::string& error);
// Validated, closed template -> fresh identities in the target scene.
bool InstantiatePrefab(const Json::Value&, GameSproutRuntime::IGameSproutRuntime&,
    GameSproutRuntime::IScene&, GameSproutRuntime::IGameObject& parent,
    GameSproutRuntime::IGameObject*& output,
    std::vector<GameSproutRuntime::PrefabObjectMapping>& mappings, std::string& error);
// 单组件快照用于编辑事务。调用者负责在完整场景中校验跨对象引用。
Json::Value SerializeComponent(GameSproutRuntime::IComponent& component, const ObjectIDs& ids);
bool RestoreComponent(const Json::Value& data, GameSproutRuntime::IGameObject& object,
                      GameSproutRuntime::IGameSproutRuntime& runtime, std::string& error);
// Subtrees can reference surviving nodes outside the subtree; full restore validates the merged scene.
bool ValidateSubtree(const Json::Value& document, std::string& error);
// document仅含一个根的子树，根ParentID=0；原位加入同一场景，不重建现有节点。
// 验证子树、存活的外部引用与合并约束，失败保留 ids/output 和原主相机。
bool RestoreSubtree(const Json::Value& document,GameSproutRuntime::IGameSproutRuntime& runtime,
                    GameSproutRuntime::IScene& scene,GameSproutRuntime::IGameObject& parent,
                    std::size_t siblingIndex,ObjectIDs& ids,GameSproutRuntime::IGameObject*& output,std::string& error);
}

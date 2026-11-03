#pragma once

#include "GameSproutRuntimeApi.h"
#include "JsonCpp/json.hpp"
#include <string>
#include <map>

namespace SceneDocument {
struct ObjectIDs {
    std::map<std::uint64_t,std::uint64_t> RuntimeToFile;
    std::uint64_t Next = 1; // 0 表示 uint64 文件 ID 空间耗尽，不能再分配。
    // Only pass live components to lookup/bind. The lifetime stamp rejects an
    // address reused by a later component, including one on another owner.
    std::uint64_t FindComponentFileID(const GameSproutRuntime::IComponent* component) const {
        if(!component||!component->GetGameObject())return 0;
        const auto identity=_ComponentToFile.find(component);
        const auto stamp=_ComponentLifetimes.find(component);
        if(identity==_ComponentToFile.end()||stamp==_ComponentLifetimes.end()||
           stamp->second.OwnerID!=component->GetGameObject()->GetInstanceID()||
           stamp->second.InstanceID!=component->GetComponentInstanceID())return 0;
        return identity->second;
    }
    void BindComponentFileID(const GameSproutRuntime::IComponent* component,std::uint64_t id) {
        if(!component||!component->GetGameObject()||!id)return;
        _ComponentToFile[component]=id;
        _ComponentLifetimes[component]={component->GetGameObject()->GetInstanceID(),component->GetComponentInstanceID()};
    }
    // Forget compares pointer keys only; a destroyed component is permitted.
    void ForgetComponentFileID(const GameSproutRuntime::IComponent* component) {
        _ComponentToFile.erase(component);_ComponentLifetimes.erase(component);
    }
    void SwapComponentIdentities(ObjectIDs& other) noexcept {
        _ComponentToFile.swap(other._ComponentToFile);_ComponentLifetimes.swap(other._ComponentLifetimes);
    }
private:
    struct ComponentLifetime {std::uint64_t OwnerID=0,InstanceID=0;};
    std::map<const GameSproutRuntime::IComponent*,std::uint64_t> _ComponentToFile;
    std::map<const GameSproutRuntime::IComponent*,ComponentLifetime> _ComponentLifetimes;
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
// 校验合并后的完整文档，失败移除本次新建节点，保留ids/output和原主相机。
bool RestoreSubtree(const Json::Value& document,GameSproutRuntime::IGameSproutRuntime& runtime,
                    GameSproutRuntime::IScene& scene,GameSproutRuntime::IGameObject& parent,
                    std::size_t siblingIndex,ObjectIDs& ids,GameSproutRuntime::IGameObject*& output,std::string& error);
}

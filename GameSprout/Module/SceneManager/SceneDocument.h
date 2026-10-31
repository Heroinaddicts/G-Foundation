#pragma once

#include "GameSproutRuntimeApi.h"
#include "JsonCpp/json.hpp"
#include <string>
#include <map>

namespace SceneDocument {
struct ObjectIDs {
    std::map<std::uint64_t,std::uint64_t> RuntimeToFile;
    std::uint64_t Next = 1; // 0 表示 uint64 文件 ID 空间耗尽，不能再分配。
};
// V1 编辑器源场景。只生成数据，不做文件 IO，也不创建预处理产物。
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
bool Validate(const Json::Value& document, std::string& error);
// Subtrees can reference surviving nodes outside the subtree; full restore validates the merged scene.
bool ValidateSubtree(const Json::Value& document, std::string& error);
// document仅含一个根的子树，根ParentID=0；原位加入同一场景，不重建现有节点。
// 校验合并后的完整文档，失败移除本次新建节点，保留ids/output和原主相机。
bool RestoreSubtree(const Json::Value& document,GameSproutRuntime::IGameSproutRuntime& runtime,
                    GameSproutRuntime::IScene& scene,GameSproutRuntime::IGameObject& parent,
                    std::size_t siblingIndex,ObjectIDs& ids,GameSproutRuntime::IGameObject*& output,std::string& error);
}

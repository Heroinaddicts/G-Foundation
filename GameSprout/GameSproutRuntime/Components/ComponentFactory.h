#pragma once
#include "Api/GameSproutRuntimeApi.h"

namespace GameSproutRuntime {
// 返回尚未挂载、尚未执行 OnCreate 的内部组件。调用方接管所有权。
IComponent* CreateBuiltinComponent(const std::type_info& type, IGameObject* owner);
// 先完整分解校验，再一次性提交 TRS；失败时保留原值。
bool SetLocalTransform(ITransformComponent* transform, const Mat4& matrix);
// 由 Runtime 对象管理器实现，排队通知编辑器；组件不依赖 Editor。
void NotifyComponentChanged(IGameObject* owner, IComponent* component);
}

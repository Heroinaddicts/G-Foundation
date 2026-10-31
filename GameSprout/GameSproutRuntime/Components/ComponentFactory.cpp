#include "ComponentFactory.h"
#include "TransformComponent.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "CanvasComponent.h"
#include "MeshComponent.h"
#include "MeshRendererComponent.h"
#include "CameraComponent.h"
#include "AnimatorComponent.h"
#include "SkinnedMeshRendererComponent.h"

namespace GameSproutRuntime {
IComponent* CreateBuiltinComponent(const std::type_info& type, IGameObject* owner) {
    if (!owner) return nullptr;
    if(owner==owner->GetScene()&&(type==typeid(IAnimatorComponent)||type==typeid(ISkinnedMeshRendererComponent)))return nullptr;
    if (type == typeid(ITransformComponent)) return new TransformComponent(owner);
    if (type == typeid(IRectTransformComponent)) return new RectTransformComponent(owner);
    if (type == typeid(IImageComponent)) return new ImageComponent(owner);
    if (type == typeid(ICanvasComponent)) return new CanvasComponent(owner);
    if (type == typeid(IMeshComponent)) return new MeshComponent(owner);
    if (type == typeid(IMeshRendererComponent)) return new MeshRendererComponent(owner);
    if (type == typeid(ICameraComponent)) return new CameraComponent(owner);
    if (type == typeid(IAnimatorComponent)) return new AnimatorComponent(owner);
    if (type == typeid(ISkinnedMeshRendererComponent)) return new SkinnedMeshRendererComponent(owner);
    return nullptr;
}

bool SetLocalTransform(ITransformComponent* transform, const Mat4& matrix) {
    auto* implementation = dynamic_cast<TransformComponent*>(transform);
    return implementation && implementation->SetLocalMatrix(matrix);
}
}

#include "ComponentFactory.h"
#include "GameSproutRuntime.h"
#include "TransformComponent.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "CanvasComponent.h"
#include "MeshComponent.h"
#include "MeshRendererComponent.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "AnimatorComponent.h"
#include "SkinnedMeshRendererComponent.h"
#include "Physics/PhysicsComponents.h"
#include "UI/UIComponents.h"

namespace GameSproutRuntime {
namespace {
template<typename Implementation>
IComponent* NewComponent(IGameObject* owner,std::uint8_t* tickMask) {
    if(tickMask)*tickMask=Runtime::ComponentTickMask<Implementation>();
    return new Implementation(owner);
}
}
IComponent* CreateBuiltinComponent(const std::type_info& type, IGameObject* owner,std::uint8_t* tickMask) {
    if (!owner) return nullptr;
    if(owner==owner->GetScene()&&(type==typeid(IAnimatorComponent)||type==typeid(ISkinnedMeshRendererComponent)))return nullptr;
    if (type == typeid(ITransformComponent)) return NewComponent<TransformComponent>(owner,tickMask);
    if (type == typeid(IRectTransformComponent)) return NewComponent<RectTransformComponent>(owner,tickMask);
    if (type == typeid(IImageComponent)) return NewComponent<ImageComponent>(owner,tickMask);
    if (type == typeid(ICanvasComponent)) return NewComponent<CanvasComponent>(owner,tickMask);
    if (type == typeid(ITextComponent)) return NewComponent<TextComponent>(owner,tickMask);
    if (type == typeid(IButtonComponent)) return NewComponent<ButtonComponent>(owner,tickMask);
    if (type == typeid(IMeshComponent)) return NewComponent<MeshComponent>(owner,tickMask);
    if (type == typeid(IMeshRendererComponent)) return NewComponent<MeshRendererComponent>(owner,tickMask);
    if (type == typeid(ICameraComponent)) return NewComponent<CameraComponent>(owner,tickMask);
    if (type == typeid(ILightComponent) && owner!=owner->GetScene()) return NewComponent<LightComponent>(owner,tickMask);
    if (type == typeid(IAnimatorComponent)) return NewComponent<AnimatorComponent>(owner,tickMask);
    if (type == typeid(ISkinnedMeshRendererComponent)) return NewComponent<SkinnedMeshRendererComponent>(owner,tickMask);
    if (owner==owner->GetScene()) return nullptr;
    return CreatePhysicsComponent(type,owner,tickMask);
}
bool AllowsMultipleBuiltinComponents(const std::type_info& type) {
    return type==typeid(IBoxColliderComponent) || type==typeid(ISphereColliderComponent) ||
        type==typeid(ICapsuleColliderComponent) || type==typeid(IMeshColliderComponent);
}

bool SetLocalTransform(ITransformComponent* transform, const Mat4& matrix) {
    auto* implementation = dynamic_cast<TransformComponent*>(transform);
    return implementation && implementation->SetLocalMatrix(matrix);
}
}

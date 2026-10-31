#include "SkinnedMeshRendererComponent.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "RuntimeMath.h"
namespace GameSproutRuntime {
SkinnedMeshRendererComponent::SkinnedMeshRendererComponent(IGameObject* object):BuiltinComponent<ISkinnedMeshRendererComponent>(object,"GameSproutRuntime.SkinnedMeshRenderer"){}
bool SkinnedMeshRendererComponent::SetAnimatorObjectID(std::uint64_t id){
    auto* access=dynamic_cast<ObjectAccess*>(_GameObject);if(!access)return false;
    auto& runtime=access->State().Owner;
    if(id){auto* state=runtime.Find(id);if(!runtime.IsUsable(state)||state->GetScene()!=_GameObject->GetScene()||!state->Object->GetComponent<IAnimatorComponent>())return false;}
    if(_AnimatorID==id)return true;_AnimatorID=id;NotifyComponentChanged(_GameObject,this);return true;
}
bool SkinnedMeshRendererComponent::GetSkinMatrices(const std::vector<SkinBinding>& bones,std::vector<Mat4>& output,std::string& error) const {
    output.clear();error.clear();auto* access=dynamic_cast<ObjectAccess*>(_GameObject);
    if(!access){error="SkinnedMeshRenderer has no live Runtime owner";return false;}
    auto& runtime=access->State().Owner;auto* state=runtime.Find(_AnimatorID);
    auto* animator=runtime.IsUsable(state)&&state->GetScene()==_GameObject->GetScene()?state->Object->GetComponent<IAnimatorComponent>():nullptr;
    if(!animator){error="SkinnedMeshRenderer Animator binding is missing";return false;}
    Mat4 inverse;if(!RuntimeMath::TryInverseAffine(_GameObject->GetTransform()->GetWorldMatrix(),inverse)){error="SkinnedMeshRenderer transform is singular";return false;}
    output.reserve(bones.size());
    for(const auto& bone:bones){std::uint64_t id=0;if(!animator->ResolveBone(bone.NodePath,id)){output.clear();error="Skin bone is missing: "+bone.NodePath;return false;}
        auto* node=runtime.Find(id);if(!runtime.IsUsable(node)){output.clear();error="Skin bone was destroyed";return false;}
        const Mat4 matrix=inverse*node->Object->GetTransform()->GetWorldMatrix()*bone.InverseBindMatrix;
        if(!RuntimeMath::IsFinite(matrix)){output.clear();error="Skin matrix is not finite";return false;}output.push_back(matrix);
    }return true;
}
}

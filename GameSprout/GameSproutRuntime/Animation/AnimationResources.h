#pragma once
#include "Api/RuntimeResourceApi.h"
#include <map>
namespace GameSproutRuntime {
// Immutable prepared data shared by all Animator instances of one Runtime.
class AnimationResources {
public:
    void SetProvider(IRuntimeResourceProvider*);
    IRuntimeResourceProvider* GetProvider() const{return _Provider;}
    std::uint64_t GetGeneration();
    std::uint64_t GetCachedGeneration() const {return _Generation;}
    const HumanoidAvatarData* Avatar(const ResourceReference&,std::string&);
    const SkeletonData* Skeleton(const ResourceReference&,std::string&);
    const AnimationClipData* Clip(const ResourceReference&,std::string&);
    const AvatarMaskData* Mask(const ResourceReference&,std::string&);
    const AnimatorControllerData* Controller(const ResourceReference&,std::string&);
private:
    void Sync();
    IRuntimeResourceProvider* _Provider=nullptr;
    std::uint64_t _Revision=0,_Generation=1;
    std::map<std::string,HumanoidAvatarData> _Avatars;
    std::map<std::string,SkeletonData> _Skeletons;
    std::map<std::string,AnimationClipData> _Clips;
    std::map<std::string,AvatarMaskData> _Masks;
    std::map<std::string,AnimatorControllerData> _Controllers;
};
}

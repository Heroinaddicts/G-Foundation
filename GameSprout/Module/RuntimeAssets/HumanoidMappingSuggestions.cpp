#include "RuntimeAssets.h"
#include "HumanoidValidation.h"
#include <algorithm>
#include <map>
#include <set>

namespace {
std::string NormalizeHumanName(const std::string& input) {
    const auto colon=input.rfind(':');std::string output;
    for(std::size_t index=colon==std::string::npos?0:colon+1;index<input.size();++index) {
        char value=input[index];if(value>='A'&&value<='Z')value=static_cast<char>(value-'A'+'a');
        if((value>='a'&&value<='z')||(value>='0'&&value<='9'))output+=value;
    }
    return output;
}
}
bool RuntimeAssets::SuggestHumanoidMapping(const GameSproutRuntime::SkeletonData& skeleton,
    std::vector<GameSproutRuntime::HumanBoneMapping>& output,std::string& error) const {
    output.clear();error.clear();if(skeleton.Nodes.empty()){error="骨架为空。";return false;}
    // Suggestions never alter the user's source settings, reference pose or axes.
    // Ambiguous equal-ranked names are deliberately left unmapped for manual selection.
    const std::map<GameSproutRuntime::HumanBone,std::vector<std::string>> aliases={
        {GameSproutRuntime::HumanBone::Hips,{"rootm","hips","pelvis"}},
        {GameSproutRuntime::HumanBone::Spine,{"spine1m","spine","spine01"}},
        {GameSproutRuntime::HumanBone::Chest,{"chestm","chest","spine1","spine02"}},
        {GameSproutRuntime::HumanBone::UpperChest,{"upperchest","spine2","spine03"}},
        {GameSproutRuntime::HumanBone::Neck,{"neckm","neck","neck1","neck01"}},
        {GameSproutRuntime::HumanBone::Head,{"headm","head"}},
        {GameSproutRuntime::HumanBone::LeftUpperLeg,{"hipl","leftupleg","thighl","uplegl"}},
        {GameSproutRuntime::HumanBone::RightUpperLeg,{"hipr","rightupleg","thighr","uplegr"}},
        {GameSproutRuntime::HumanBone::LeftLowerLeg,{"kneel","leftleg","calfl","legl"}},
        {GameSproutRuntime::HumanBone::RightLowerLeg,{"kneer","rightleg","calfr","legr"}},
        {GameSproutRuntime::HumanBone::LeftFoot,{"anklel","leftfoot","footl"}},
        {GameSproutRuntime::HumanBone::RightFoot,{"ankler","rightfoot","footr"}},
        {GameSproutRuntime::HumanBone::LeftToes,{"toesl","lefttoebase","balll"}},
        {GameSproutRuntime::HumanBone::RightToes,{"toesr","righttoebase","ballr"}},
        {GameSproutRuntime::HumanBone::LeftShoulder,{"scapulal","leftshoulder","claviclel"}},
        {GameSproutRuntime::HumanBone::RightShoulder,{"scapular","rightshoulder","clavicler"}},
        {GameSproutRuntime::HumanBone::LeftUpperArm,{"shoulderl","leftarm","upperarml","arml"}},
        {GameSproutRuntime::HumanBone::RightUpperArm,{"shoulderr","rightarm","upperarmr","armr"}},
        {GameSproutRuntime::HumanBone::LeftLowerArm,{"elbowl","leftforearm","lowerarml","forearml"}},
        {GameSproutRuntime::HumanBone::RightLowerArm,{"elbowr","rightforearm","lowerarmr","forearmr"}},
        {GameSproutRuntime::HumanBone::LeftHand,{"wristl","lefthand","handl"}},
        {GameSproutRuntime::HumanBone::RightHand,{"wristr","righthand","handr"}},
        {GameSproutRuntime::HumanBone::LeftMiddleProximal,{"middlefinger1l","leftmiddle1"}},
        {GameSproutRuntime::HumanBone::LeftMiddleIntermediate,{"middlefinger2l","leftmiddle2"}},
        {GameSproutRuntime::HumanBone::LeftMiddleDistal,{"middlefinger3l","leftmiddle3"}},
        {GameSproutRuntime::HumanBone::RightMiddleProximal,{"middlefinger1r","rightmiddle1"}},
        {GameSproutRuntime::HumanBone::RightMiddleIntermediate,{"middlefinger2r","rightmiddle2"}},
        {GameSproutRuntime::HumanBone::RightMiddleDistal,{"middlefinger3r","rightmiddle3"}}
    };
    std::set<std::string> used;
    for(unsigned index=0;index<static_cast<unsigned>(GameSproutRuntime::HumanBone::Count);++index) {
        const auto bone=static_cast<GameSproutRuntime::HumanBone>(index);std::vector<std::string> names;
        const auto found=aliases.find(bone);if(found!=aliases.end())names=found->second;
        names.push_back(NormalizeHumanName(GameSproutRuntime::HumanBoneName(bone)));
        int best=-1;std::string selected;bool ambiguous=false;
        for(const auto& node:skeleton.Nodes) {
            const auto name=NormalizeHumanName(node.Name);if(used.count(node.Path))continue;
            const auto matched=std::find(names.begin(),names.end(),name);if(matched==names.end())continue;
            const int rank=100-static_cast<int>(matched-names.begin());
            if(rank>best){best=rank;selected=node.Path;ambiguous=false;}else if(rank==best)ambiguous=true;
        }
        if(best>=0&&!ambiguous){GameSproutRuntime::HumanBoneMapping mapping;mapping.Bone=bone;mapping.Path=selected;output.push_back(mapping);used.insert(selected);}
    }
    return true;
}

#pragma once
#include "../../Module/Shared/HumanoidDefinition.h"
#include <thread>

namespace {
std::vector<std::pair<GameSproutRuntime::HumanBone,std::string>> WomanHumanBones() {
    return {
        {GameSproutRuntime::HumanBone::Hips,"Root_M"},{GameSproutRuntime::HumanBone::Spine,"Spine1_M"},
        {GameSproutRuntime::HumanBone::Chest,"Chest_M"},{GameSproutRuntime::HumanBone::Neck,"Neck_M"},
        {GameSproutRuntime::HumanBone::Head,"Head_M"},{GameSproutRuntime::HumanBone::LeftShoulder,"Scapula_L"},
        {GameSproutRuntime::HumanBone::RightShoulder,"Scapula_R"},{GameSproutRuntime::HumanBone::LeftUpperArm,"Shoulder_L"},
        {GameSproutRuntime::HumanBone::RightUpperArm,"Shoulder_R"},{GameSproutRuntime::HumanBone::LeftLowerArm,"Elbow_L"},
        {GameSproutRuntime::HumanBone::RightLowerArm,"Elbow_R"},{GameSproutRuntime::HumanBone::LeftHand,"Wrist_L"},
        {GameSproutRuntime::HumanBone::RightHand,"Wrist_R"},{GameSproutRuntime::HumanBone::LeftUpperLeg,"Hip_L"},
        {GameSproutRuntime::HumanBone::RightUpperLeg,"Hip_R"},{GameSproutRuntime::HumanBone::LeftLowerLeg,"Knee_L"},
        {GameSproutRuntime::HumanBone::RightLowerLeg,"Knee_R"},{GameSproutRuntime::HumanBone::LeftFoot,"Ankle_L"},
        {GameSproutRuntime::HumanBone::RightFoot,"Ankle_R"},{GameSproutRuntime::HumanBone::LeftToes,"Toes_L"},
        {GameSproutRuntime::HumanBone::RightToes,"Toes_R"},{GameSproutRuntime::HumanBone::LeftMiddleProximal,"MiddleFinger1_L"},
        {GameSproutRuntime::HumanBone::LeftMiddleIntermediate,"MiddleFinger2_L"},{GameSproutRuntime::HumanBone::LeftMiddleDistal,"MiddleFinger3_L"},
        {GameSproutRuntime::HumanBone::RightMiddleProximal,"MiddleFinger1_R"},{GameSproutRuntime::HumanBone::RightMiddleIntermediate,"MiddleFinger2_R"},
        {GameSproutRuntime::HumanBone::RightMiddleDistal,"MiddleFinger3_R"}
    };
}
std::string HumanPeerName(const std::string& source) {return "X"+source.substr(1);}
ModelHumanoidImportSettings WomanHumanProfile(const GameSproutRuntime::SkeletonData& skeleton,bool renamed) {
    ModelHumanoidImportSettings settings;
    for(const auto& entry:WomanHumanBones()) {
        const std::string name=renamed?HumanPeerName(entry.second):entry.second;
        const auto found=std::find_if(skeleton.Nodes.begin(),skeleton.Nodes.end(),[&](const auto& node){return node.Name==name;});
        Check(found!=skeleton.Nodes.end(),"Humanoid fixture lacks bone "+name);
        settings.HumanBones.push_back({entry.first,found->Path,{}});
    }
    return settings;
}
AssetImportRequest HumanModelRequest(const QTemporaryDir& root,const Guid& guid,const std::string& path,
    const ModelHumanoidImportSettings& settings,float scale) {
    auto request=Request(root,guid,path,eAssetImporterType::Model);Json::Value meta;
    Check(AssetArtifactIO::Parse(request.MetaJson,meta),"Cannot parse Humanoid model request");
    meta["ImportSettings"]=HumanoidDefinition::SettingsJson(settings);
    meta["ImportSettings"]["AnimationType"]="Humanoid";meta["ImportSettings"]["ScaleFactor"]=scale;
    request.MetaJson=AssetArtifactIO::JsonText(meta);return request;
}
void WriteHumanPeer(const QTemporaryDir& root) {
    QFile source(root.path()+"/Assets/WomanAnimations.fbx");Check(source.open(QIODevice::ReadOnly),"Cannot read source FBX fixture");
    QByteArray bytes=source.readAll();
    // FBX model names are uncompressed string properties. Equal-byte-length replacements
    // retain binary record offsets and connection IDs; ufbx verifies the resulting file.
    for(const auto& entry:WomanHumanBones()) {
        const QByteArray oldName=QByteArray::fromStdString(entry.second);
        const QByteArray newName=QByteArray::fromStdString(HumanPeerName(entry.second));
        Check(bytes.contains(oldName)&&oldName.size()==newName.size(),"FBX name replacement fixture is invalid");
        bytes.replace(oldName,newName);
    }
    std::string error;
    Check(AssetArtifactIO::Write(root.path()+"/Assets/HumanoidTarget.fbx",bytes,error),error);
}
Json::Value NativeHumanReferenceClip(const GameSproutRuntime::SkeletonData& skeleton,
    const GameSproutRuntime::HumanoidAvatarData& avatar) {
    Check(skeleton.Nodes.size()==avatar.ReferencePose.size(),"Native human reference pose has the wrong Skeleton");
    Json::Value source;source["Version"]=1;source["Duration"]=1;source["SampleRate"]=1;
    source["Tracks"]=Json::Value(Json::arrayValue);source["Parents"]=Json::Value(Json::arrayValue);
    source["Frames"]=Json::Value(Json::arrayValue);
    for(const auto& node:skeleton.Nodes){source["Tracks"].append(node.Path);source["Parents"].append(node.Parent);}
    for(unsigned sample=0;sample<2;++sample) {
        Json::Value frame;frame["Time"]=sample;frame["Poses"]=Json::Value(Json::arrayValue);
        for(const auto& pose:avatar.ReferencePose)frame["Poses"].append(HumanoidDefinition::Pose(pose));
        source["Frames"].append(std::move(frame));
    }
    return source;
}
GameSproutRuntime::IGameObject* HumanNode(GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IAnimatorComponent& animator,const GameSproutRuntime::HumanoidAvatarData& avatar,
    GameSproutRuntime::HumanBone role) {
    for(const auto& bone:avatar.Bones)if(bone.Bone==role) {
        std::uint64_t id=0;Check(animator.ResolveBone(bone.Path,id),"Humanoid role was not bound");
        auto* object=runtime.QueryGameObject(id);Check(object,"Humanoid bone object was deleted");return object;
    }
    throw std::runtime_error("Humanoid Avatar lacks required test role");
}
GameSproutRuntime::Vector3 HumanPosition(const GameSproutRuntime::IGameObject& object) {
    const auto matrix=object.GetTransform()->GetWorldMatrix();return {matrix.Data[12],matrix.Data[13],matrix.Data[14]};
}
float HumanDistance(const GameSproutRuntime::Vector3& left,const GameSproutRuntime::Vector3& right) {
    const auto delta=left-right;return std::sqrt(delta.X*delta.X+delta.Y*delta.Y+delta.Z*delta.Z);
}
class HumanIKProbe final : public GameSproutRuntime::IRuntimeListener {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;std::uint64_t Object=0;
    GameSproutRuntime::AnimatorIKGoal Goal;bool Enable=false;unsigned AuthoringChanges=0;
    std::vector<std::uint32_t> Passes;
    const std::thread::id Thread=std::this_thread::get_id();
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++AuthoringChanges;
    }
    void OnAnimatorIK(const GameSproutRuntime::AnimatorIKNotification& event) override {
        if(event.ObjectID!=Object)return;
        Check(std::this_thread::get_id()==Thread,"IK callback ran outside the host thread");Passes.push_back(event.LayerIndex);
        if(!Enable)return;auto* object=Runtime->QueryGameObject(Object);
        auto* animator=object?object->GetComponent<GameSproutRuntime::IAnimatorComponent>():nullptr;
        Check(animator&&animator->SetIKGoal(GameSproutRuntime::AvatarIKGoal::LeftHand,Goal),"IK pass rejected a valid hand goal");
    }
};
}

#pragma once
#include "../../Module/Shared/AvatarMaskDefinition.h"

namespace {
void PersistLayerMeta(const QTemporaryDir& root,const AssetImportJob& job) {
    Json::Value meta;std::string error;
    Check(AssetArtifactIO::Parse(job.Request.MetaJson,meta),"Cannot parse layer test metadata");
    Check(AssetArtifactIO::Parse(job.Result.Manifest.SubAssetsJson,meta["SubAssets"]),"Cannot parse layer subresources");
    Check(AssetArtifactIO::Write(root.path()+"/Assets/"+QString::fromStdString(job.Request.SourcePath)+".meta",
        QByteArray::fromStdString(AssetArtifactIO::JsonText(meta)),error),error);
}
void CapturePreparedResources(AssetImportRequest& request,const Library& library) {
    for(const auto& entry:library.Resources) {
        AssetImportPreparedResource value;value.Type=entry.second.Artifact.Type;
        value.Path=entry.second.Path;value.DescriptorJson=entry.second.Artifact.DescriptorJson;
        value.Hash=entry.second.Artifact.Hash;
        value.Size=entry.second.Artifact.DataSize;value.Version=entry.second.Artifact.DataVersion;
        request.PreparedResources.emplace(entry.first,std::move(value));
    }
}
Json::Value UpperBodySource(const GameSproutRuntime::SkeletonData& skeleton,std::size_t chest,std::size_t hip) {
    Json::Value source;source["Version"]=1;source["Duration"]=1;source["SampleRate"]=1;
    source["Tracks"]=Json::Value(Json::arrayValue);source["Parents"]=Json::Value(Json::arrayValue);
    source["Frames"]=Json::Value(Json::arrayValue);
    for(const auto& node:skeleton.Nodes){source["Tracks"].append(node.Path);source["Parents"].append(node.Parent);}
    for(unsigned sample=0;sample<2;++sample) {
        Json::Value frame;frame["Time"]=sample;frame["Poses"]=Json::Value(Json::arrayValue);
        for(std::size_t index=0;index<skeleton.Nodes.size();++index) {
            const auto& pose=skeleton.Nodes[index].Transform;auto rotation=pose.Rotation;
            if(index==chest&&sample) {
                const GameSproutRuntime::Quaternion turn{0,0,std::sqrt(.5f),std::sqrt(.5f)};
                rotation={rotation.W*turn.X+rotation.X*turn.W+rotation.Y*turn.Z-rotation.Z*turn.Y,
                    rotation.W*turn.Y-rotation.X*turn.Z+rotation.Y*turn.W+rotation.Z*turn.X,
                    rotation.W*turn.Z+rotation.X*turn.Y-rotation.Y*turn.X+rotation.Z*turn.W,
                    rotation.W*turn.W-rotation.X*turn.X-rotation.Y*turn.Y-rotation.Z*turn.Z};
            }
            Json::Value value;
            // The lower-body displacement is intentional: the production mask must remove it.
            value["Translation"]=MotionValues({pose.Position.X+(index==hip?sample*.25f:0),pose.Position.Y,pose.Position.Z});
            value["Rotation"]=MotionValues({rotation.X,rotation.Y,rotation.Z,rotation.W});
            value["Scale"]=MotionValues({pose.Scale.X,pose.Scale.Y,pose.Scale.Z});frame["Poses"].append(std::move(value));
        }
        source["Frames"].append(std::move(frame));
    }
    return source;
}
bool LayerMatrixClose(const GameSproutRuntime::Mat4& left,const GameSproutRuntime::Mat4& right,float tolerance=.0002f) {
    for(unsigned index=0;index<16;++index)if(std::fabs(left.Data[index]-right.Data[index])>tolerance)return false;
    return true;
}
class LayerProbe final : public GameSproutRuntime::IRuntimeListener {
public:
    std::uint64_t Object=0;unsigned AuthoringChanges=0;
    std::vector<GameSproutRuntime::AnimationNotification> Events;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent& event) override {
        if(event.Type==GameSproutRuntime::SceneNodeEventType::ComponentChanged)++AuthoringChanges;
    }
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) override {
        if(event.ObjectID==Object&&event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent)Events.push_back(event);
    }
};
}

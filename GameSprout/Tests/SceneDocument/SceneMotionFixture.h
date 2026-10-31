#pragma once
#include "RuntimeResourceApi.h"

class SceneMotionResources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override {return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&) override {return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override {return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&) override {return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&) override {
        data={};GameSproutRuntime::SkeletonNode node;node.Name="Bone";node.Path="/Bone#0";
        data.Nodes.push_back(node);return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimationClipData& data,std::string&) override {
        data={};data.Duration=1;data.SampleRate=1;data.Tracks={"/Bone#0"};
        GameSproutRuntime::AnimationFrame first,last;first.Time=0;last.Time=1;
        first.Poses.resize(1);last.Poses.resize(1);last.Poses[0].Position={2,0,0};
        data.Frames={first,last};data.RootMotionNodePath="/Bone#0";
        data.RootMotion.resize(2);data.RootMotion.back().Translation={2,0,0};
        data.Events={{0.25f,"Footstep","left"},{0.75f,"Footstep","right"}};
        return true;
    }
};
class SceneMotionListener final : public GameSproutRuntime::IRuntimeListener {
public:
    std::uint64_t Object=0;
    int Steps=0,Enters=0,Moves=0;
    bool Consume=false;
    void OnSceneNodeEvent(const GameSproutRuntime::SceneNodeEvent&) override {}
    void OnAnimationEvent(const GameSproutRuntime::AnimationNotification& event) override {
        if(event.ObjectID!=Object)return;
        if(event.Type==GameSproutRuntime::AnimationNotificationType::ClipEvent&&event.Name=="Footstep")++Steps;
        if(event.Type==GameSproutRuntime::AnimationNotificationType::StateEnter)++Enters;
    }
    bool OnRootMotion(const GameSproutRuntime::RootMotionNotification& event) override {
        if(event.ObjectID!=Object)return false;
        ++Moves;return Consume;
    }
};

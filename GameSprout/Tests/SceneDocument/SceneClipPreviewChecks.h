#pragma once
#include "RuntimeResourceApi.h"
#include <QThread>

class SceneClipPreviewResources final : public GameSproutRuntime::IRuntimeResourceProvider {
public:
    std::uint64_t GetRevision() const override {return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&) override {return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override {return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&) override {return false;}
    bool LoadSkeleton(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::SkeletonData& data,std::string&) override {
        data={};data.Nodes.push_back({"/Bone#0","Bone",-1,{}});return true;
    }
    bool LoadAnimationClip(const GameSproutRuntime::ResourceReference& reference,GameSproutRuntime::AnimationClipData& data,std::string& error) override {
        if(reference.LocalID!=2&&reference.LocalID!=3){error="Missing test Clip";return false;}
        data={};data.Duration=1;data.SampleRate=1;data.Tracks={"/Bone#0"};
        GameSproutRuntime::AnimationFrame first,last;first.Time=0;last.Time=1;
        first.Poses.resize(1);last.Poses.resize(1);
        first.Poses[0].Position.X=reference.LocalID==2?0.f:10.f;
        last.Poses[0].Position.X=first.Poses[0].Position.X+2.f;
        data.Frames={first,last};return true;
    }
    bool LoadAnimatorController(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::AnimatorControllerData& data,std::string&) override {
        data={};data.Parameters={{"Speed",GameSproutRuntime::AnimatorParameterType::Float,0}};
        GameSproutRuntime::AnimatorLayer layer;layer.Name="Base";
        layer.States={{"Idle",{"0123456789abcdef0123456789abcdef",2}},{"Run",{"0123456789abcdef0123456789abcdef",3}}};
        GameSproutRuntime::AnimatorTransition transition;transition.From=-1;transition.To=0;transition.Duration=0;
        transition.Conditions={{"Speed",GameSproutRuntime::AnimatorCompare::Less,.1}};
        layer.Transitions={transition};data.Layers={layer};return true;
    }
};

static void CheckDirectClipPreview(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& api,const QDir& root,IRuntimeHost& host,IAnimationPreview& preview,GameSproutRuntime::IGameSproutRuntime* runtime) {
    auto* previous=runtime->GetResourceProvider();
    SceneClipPreviewResources resources;runtime->SetResourceProvider(&resources);
    struct ResourceScope {
        GameSproutRuntime::IGameSproutRuntime* Runtime;
        GameSproutRuntime::IRuntimeResourceProvider* Previous;
        ~ResourceScope(){Runtime->SetResourceProvider(Previous);}
    } resourceScope{runtime,previous};
    const std::string guid="0123456789abcdef0123456789abcdef";
    const auto create=[&](const std::string& name){
        auto* character=manager.GetActiveScene()->CreateGameObject(name);
        auto* bone=manager.GetActiveScene()->CreateGameObject("Bone",character);
        bone->GetTransform()->SetLocalPosition({7,0,0});
        auto* animator=character->AddComponent<GameSproutRuntime::IAnimatorComponent>();
        animator->SetSkeleton({guid,1});animator->SetController({guid,4});animator->SetDefaultClip({guid,2});
        Check(animator->SetNodeBindings({{"/Bone#0",bone->GetInstanceID()}}),"Cannot bind Clip preview fixture");
        return character;
    };
    auto* first=create("Clip preview first");auto* second=create("Clip preview second");
    auto* firstAnimator=first->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    auto* secondAnimator=second->GetComponent<GameSproutRuntime::IAnimatorComponent>();
    const auto firstID=first->GetInstanceID(),secondID=second->GetInstanceID();
    Check(firstAnimator->Play("Idle",.2f)&&secondAnimator->Play("Run",.3f),"Cannot prepare controller preview");
    std::string error;
    GameSproutEditor::GameSproutEditorFinal::Update(preview,api);GameSproutEditor::GameSproutEditorFinal::Update(host,api);GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(manager.SaveSceneAs("Scenes/ClipPreview.scene",error),error);
    const QByteArray before=Read(root.filePath("Assets/Scenes/ClipPreview.scene"));
    const bool undoBefore=manager.CanUndo(),redoBefore=manager.CanRedo();
    Check(preview.PlayAnimationClip(firstID,{guid,3},.25f,1,true,error),error);
    Check(!preview.IsAnimationPreviewPlaying()&&preview.GetAnimationClipPreviewState(firstID).Active,
        "Direct preview did not replace the Controller clock");
    Check(std::fabs(first->FindChild("Bone")->GetTransform()->GetWorldMatrix()(0,3)-10.5f)<.002f,
        "Direct preview did not evaluate its selected Clip immediately");
    const float initialClipTime=preview.GetAnimationClipPreviewState(firstID).Time;
    QThread::msleep(5);
    GameSproutEditor::GameSproutEditorFinal::Update(preview,api);
    GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(preview.GetAnimationClipPreviewState(firstID).Time==initialClipTime,
        "Preview or SceneManager advanced Runtime; only RuntimeHost may tick it");
    GameSproutEditor::GameSproutEditorFinal::Update(host,api);
    Check(preview.GetAnimationClipPreviewState(firstID).Time>initialClipTime,
        "RuntimeHost did not advance the preview's shared Runtime clock");
    const float otherTime=secondAnimator->GetPlaybackState().Time;
    QThread::msleep(20);GameSproutEditor::GameSproutEditorFinal::Update(preview,api);GameSproutEditor::GameSproutEditorFinal::Update(host,api);GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(preview.GetAnimationClipPreviewState(firstID).Time>.25f&&secondAnimator->GetPlaybackState().Time==otherTime,
        "Direct preview did not advance independently of other Animators");
    Check(preview.SetAnimationClipPreviewPlaying(firstID,false,error),error);
    const float paused=preview.GetAnimationClipPreviewState(firstID).Time;
    QThread::msleep(10);GameSproutEditor::GameSproutEditorFinal::Update(preview,api);GameSproutEditor::GameSproutEditorFinal::Update(host,api);GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(preview.GetAnimationClipPreviewState(firstID).Time==paused,"Paused Clip advanced on the Module clock");
    Check(preview.SeekAnimationClipPreview(firstID,.5f,error),error);
    Check(std::fabs(first->FindChild("Bone")->GetTransform()->GetWorldMatrix()(0,3)-11.f)<.002f,
        "Seek did not display the paused Clip pose");
    Check(!preview.PlayAnimationClip(secondID,{guid,99},0,1,true,error)&&preview.GetAnimationClipPreviewState(firstID).Active,
        "Failed object/Clip replacement discarded the old preview");
    Check(preview.PlayAnimationClip(secondID,{guid,2},.2f,2,false,error),error);
    Check(!preview.GetAnimationClipPreviewState(firstID).Active&&preview.GetAnimationClipPreviewState(secondID).Active
        &&std::fabs(first->FindChild("Bone")->GetTransform()->GetWorldMatrix()(0,3)-7.f)<.002f,
        "Changing target did not restore the old object's authoring pose");
    preview.StopAnimationClipPreview(firstID);
    Check(preview.GetAnimationClipPreviewState(secondID).Active,"Stale panel stopped a different object's preview");
    Check(!manager.HasUnsavedChanges()&&manager.CanUndo()==undoBefore&&manager.CanRedo()==redoBefore,
        "Clip preview polluted the scene baseline or history");
    Check(manager.SaveScene(error)&&Read(root.filePath("Assets/Scenes/ClipPreview.scene"))==before,
        "Saving during preview persisted temporary playback values");
    preview.StopAnimationClipPreview(secondID);
    Check(!preview.GetAnimationClipPreviewState(secondID).Active&&
        std::fabs(second->FindChild("Bone")->GetTransform()->GetWorldMatrix()(0,3)-7.f)<.002f,
        "Stop did not restore authoring transforms");
    double speed=99;
    Check(firstAnimator->GetController()==GameSproutRuntime::ResourceReference{guid,4}&&
        firstAnimator->GetDefaultClip()==GameSproutRuntime::ResourceReference{guid,2}&&
        firstAnimator->GetParameter("Speed",speed)&&speed==0,"Direct preview changed Controller settings or parameters");
    Check(preview.PlayAnimationClip(firstID,{guid,3},0,1,true,error),error);
    preview.SetAnimationPreviewPlaying(true);
    Check(!preview.GetAnimationClipPreviewState(firstID).Active&&preview.IsAnimationPreviewPlaying(),
        "Controller preview did not exit direct Clip preview");
    preview.ResetAnimationPreview();
    Check(preview.PlayAnimationClip(firstID,{guid,3},0,1,true,error)&&manager.OpenScene("Scenes/ClipPreview.scene",error),error);
    Check(!preview.GetAnimationClipPreviewState(firstID).Active&&!preview.IsAnimationPreviewPlaying()&&!manager.HasUnsavedChanges(),
        "Opening a scene retained the old Clip preview session");
    auto* reopened=manager.GetActiveScene()->FindChild("Clip preview first");
    Check(reopened&&preview.PlayAnimationClip(reopened->GetInstanceID(),{guid,3},0,1,true,error),error);
    const auto deletedID=reopened->GetInstanceID();
    Check(manager.DeleteObject(deletedID,error),error);
    GameSproutEditor::GameSproutEditorFinal::Update(preview,api);GameSproutEditor::GameSproutEditorFinal::Update(host,api);GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
    Check(!preview.GetAnimationClipPreviewState(deletedID).Active,"Deleting a preview target kept a dangling session");
    preview.ResetAnimationPreview();
}

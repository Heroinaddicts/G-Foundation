#pragma once
#include "../../Module/Shared/AnimationImportSettingsJson.h"

static Json::Value MotionArray(std::initializer_list<double> values) {
    Json::Value array(Json::arrayValue);for(double value:values)array.append(value);return array;
}
static Json::Value MotionSource() {
    Json::Value source;source["Version"]=1;source["Duration"]=1;source["SampleRate"]=1;
    source["Tracks"]=Json::Value(Json::arrayValue);source["Tracks"].append("/Parent");source["Tracks"].append("/Parent/Root");
    source["Parents"]=Json::Value(Json::arrayValue);source["Parents"].append(-1);source["Parents"].append(0);
    source["Frames"]=Json::Value(Json::arrayValue);
    const double sine=std::sqrt(0.5);
    for(unsigned index=0;index<2;++index) {
        Json::Value frame;frame["Time"]=index;frame["Poses"]=Json::Value(Json::arrayValue);
        Json::Value parent;parent["Translation"]=MotionArray({10,0,0});parent["Rotation"]=MotionArray({0,sine,0,sine});parent["Scale"]=MotionArray({2,2,2});
        frame["Poses"].append(parent);
        Json::Value root;root["Translation"]=MotionArray({1.0+index,0,0});root["Rotation"]=MotionArray({0,0,0,1});root["Scale"]=MotionArray({1,1,1});
        frame["Poses"].append(root);source["Frames"].append(frame);
    }
    return source;
}
static void CheckMotionSettings() {
    AnimationClipImportOptions options;options.Name="Walk";options.ExtractRootMotion=true;options.RootMotionNode="/Root";
    options.Events={{0.25f,"Footstep","Left"}};
    ModelImportSettings model;Json::Value value;
    Check(model.SetAnimationClips({options})&&model.ConvertToJson(value),"Cannot serialize model animation settings");
    ModelImportSettings restored;Check(restored.ConvertFromJson(value)&&restored.GetAnimationClips().size()==1
        &&restored.GetAnimationClips()[0].Events[0].Payload=="Left","Model animation Meta settings did not roundtrip");
    auto malformed=value;malformed["AnimationClips"][0]["Events"][0]["Time"]=-1;
    Check(!restored.ConvertFromJson(malformed)&&restored.GetAnimationClips()[0].Events[0].Time==0.25f,
        "Failed model settings validation changed the previous value");
    options.Name.clear();AnimationClipImportSettings native;native.SetMainObjectID(77);
    Check(native.SetClipOptions(options)&&native.ConvertToJson(value),"Cannot serialize native animation settings");
    AnimationClipImportSettings nativeRead;
    Check(nativeRead.ConvertFromJson(value)&&nativeRead.GetMainObjectID()==77&&nativeRead.GetClipOptions().ExtractRootMotion,
        "Native animation Meta settings or main identity did not roundtrip");
    malformed=value;malformed["ClipOptions"]["Unknown"]=true;
    Check(!nativeRead.ConvertFromJson(malformed)&&nativeRead.GetMainObjectID()==77,"Invalid ClipOptions changed prior settings");
    Check(nativeRead.ConvertFromJson(Json::Value(Json::objectValue))&&!nativeRead.GetClipOptions().ExtractRootMotion
        &&nativeRead.GetClipOptions().Events.empty(),"Native animation defaults require explicit new settings");
}
static void CheckMotionAssets() {
    CheckMotionSettings();
    QTemporaryDir root;Check(root.isValid()&&QDir(root.path()).mkpath("Assets"),"Cannot create root motion fixture");
    TestMeta meta(eAssetImporterType::AnimationClip,Guid(100,200,300,400),"Root.anim");
    AnimationClipImportOptions options;options.ExtractRootMotion=true;options.RootMotionNode="/Parent/Root";
    options.Events={{0,"Begin","start"},{0.5f,"Footstep","Left"},{1,"End","finish"}};
    auto settings=AnimationImportSettingsJson::Write(options,false);
    AnimationClipImportOptions read;
    Check(AnimationImportSettingsJson::Read(settings,read,false)&&read.Events.size()==3,"Animation settings roundtrip failed");
    const auto requestFor=[&] {
        auto request=Request(root,meta);Json::Value data;AssetArtifactIO::Parse(request.MetaJson,data);
        data["ImportSettings"]["ClipOptions"]=settings;request.MetaJson=AssetArtifactIO::JsonText(data);return request;
    };
    auto source=MotionSource();Write(root.path()+"/Assets/Root.anim",source);
    Context context;AssetImportJob job(requestFor());Check(RunAssetImport(job,context),job.Result.Error);
    Library library;library.Add(job);LibraryRuntimeResources provider(nullptr,&library);
    GameSproutRuntime::AnimationClipData clip;std::string error;
    Check(provider.LoadAnimationClip(Ref(job,eAssetType::AnimationClip),clip,error),error);
    Check(clip.RootMotionNodePath=="/Parent/Root"&&clip.RootMotion.size()==2,"Root motion samples or source identity missing");
    Check(clip.RootMotion.front().Translation.X==0&&clip.RootMotion.front().Rotation.W==1,"Root trajectory did not start at identity");
    const auto delta=clip.RootMotion.back().Translation;
    Check(std::fabs(delta.X)<0.0001f&&std::fabs(delta.Y)<0.0001f&&std::fabs(delta.Z+2)<0.0001f,
        "Root motion ignored the selected node's parent rotation or scale");
    Check(clip.Frames.back().Poses[1].Position.X==2,"Root extraction destructively changed the original pose");
    Check(clip.Events.size()==3&&clip.Events[1].Name=="Footstep"&&clip.Events[1].Payload=="Left","Events did not reach prepared Runtime data");
    const auto fail=[&](const Json::Value& document,const Json::Value& desired,const std::string& message) {
        Write(root.path()+"/Assets/Root.anim",document);auto request=requestFor();Json::Value data;
        AssetArtifactIO::Parse(request.MetaJson,data);data["ImportSettings"]["ClipOptions"]=desired;request.MetaJson=AssetArtifactIO::JsonText(data);
        AssetImportJob invalid(request);Check(!RunAssetImport(invalid,context),message);
    };
    auto bad=settings;bad["RootMotionNode"]="/Missing";fail(source,bad,"Missing root-motion path was accepted");
    bad=settings;bad["Events"][1]["Time"]=2;fail(source,bad,"Event beyond clip duration was accepted");
    bad=source;bad.removeMember("Parents");fail(bad,settings,"Multitrack root motion guessed missing hierarchy");
    bad=source;bad["Parents"][0]=1;fail(bad,settings,"Cyclic animation hierarchy was accepted");
    bad=source;
    for(auto& frame:bad["Frames"]) {
        frame["Poses"][0]["Scale"]=MotionArray({2,1,1});
        frame["Poses"][1]["Rotation"]=MotionArray({0,0,std::sin(0.3),std::cos(0.3)});
    }
    fail(bad,settings,"Sheared root motion was silently approximated");
    auto noMotion=settings;noMotion["ExtractRootMotion"]=false;noMotion["RootMotionNode"]="";noMotion["Events"]=Json::Value(Json::arrayValue);
    Write(root.path()+"/Assets/Root.anim",source);
    auto disabled=requestFor();Json::Value metaData;AssetArtifactIO::Parse(disabled.MetaJson,metaData);metaData["ImportSettings"]["ClipOptions"]=noMotion;
    disabled.MetaJson=AssetArtifactIO::JsonText(metaData);AssetImportJob plain(disabled);Check(RunAssetImport(plain,context),plain.Result.Error);
    Library plainLibrary;plainLibrary.Add(plain);LibraryRuntimeResources plainProvider(nullptr,&plainLibrary);
    Check(plainProvider.LoadAnimationClip(Ref(plain,eAssetType::AnimationClip),clip,error)&&clip.RootMotion.empty()&&clip.Events.empty(),"Disabled motion defaults changed");
    auto& artifact=plainLibrary.Resources.begin()->second;Json::Value descriptor;
    Check(AssetArtifactIO::Parse(artifact.Artifact.DescriptorJson,descriptor),"Cannot inspect prepared descriptor");
    descriptor.removeMember("Format");artifact.Artifact.DescriptorJson=AssetArtifactIO::JsonText(descriptor);
    Check(!plainProvider.LoadAnimationClip(Ref(plain,eAssetType::AnimationClip),clip,error),"Outdated AnimationClip descriptor was accepted");
    std::cout<<"Root motion/events: parent-space conversion, original poses, settings, validation and current descriptor format passed\n";
}

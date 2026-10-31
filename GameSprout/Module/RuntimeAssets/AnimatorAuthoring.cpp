#include "RuntimeAssets.h"
#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "../Shared/AnimationDefinition.h"
#include "../Shared/AssetArtifactIO.h"
#include <algorithm>
#include <cctype>

namespace {
std::string Lower(std::string text){for(char& c:text)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return text;}
bool ReadSource(const std::string& path,Json::Value& document,std::string& error){
    QFile file(QString::fromStdString(path));
    if(!file.open(QIODevice::ReadOnly)||file.size()<0||file.size()>8*1024*1024){error="无法读取 Controller 源文件，或超过 8 MiB。";return false;}
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError||!AssetArtifactIO::Parse(bytes.toStdString(),document)){error="Controller 源文件不是有效 JSON。";return false;}
    return true;
}
bool ValidClips(ILibraryManager* library,LibraryRuntimeResources* resources,const GameSproutRuntime::AnimatorControllerData& data,std::string& error){
    GameSproutRuntime::ResourceReference skeletonReference;
    for(const auto& layer:data.Layers) {
        GameSproutRuntime::SkeletonData skeleton;
        if(!layer.Mask.Guid.empty()) {
            GameSproutRuntime::AvatarMaskData mask;
            if(!resources->LoadAvatarMask(layer.Mask,mask,error)||(mask.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!resources->LoadSkeleton(mask.Skeleton,skeleton,error)))return false;
            if(!skeletonReference.Guid.empty()&&!mask.Skeleton.Guid.empty()&&skeletonReference!=mask.Skeleton){error="各层 Mask 必须使用同一骨架。";return false;}
            if(!mask.Skeleton.Guid.empty())skeletonReference=mask.Skeleton;
        }
        std::set<std::string> paths;for(const auto& node:skeleton.Nodes)paths.insert(node.Path);
        std::vector<GameSproutRuntime::ResourceReference> refs;
        for(const auto& state:layer.States) {
            if(state.BlendParameter.empty())refs.push_back(state.Clip);else for(const auto& motion:state.Motions)refs.push_back(motion.Clip);
        }
        if(!layer.ReferenceClip.Guid.empty())refs.push_back(layer.ReferenceClip);
        for(const auto& motion:layer.MotionOverrides){if(motion.BlendParameter.empty())refs.push_back(motion.Clip);else for(const auto& child:motion.Motions)refs.push_back(child.Clip);}
        for(const auto& ref:refs) {
            IAsset* asset=library->QueryAsset(Guid(ref.Guid),ref.LocalID);
            if(!asset||asset->GetAssetType()!=eAssetType::AnimationClip){error="Controller 动作引用不存在或不是 AnimationClip。";return false;}
            if(paths.empty()&&ref!=layer.ReferenceClip)continue;
            GameSproutRuntime::AnimationClipData clip;if(!resources->LoadAnimationClip(ref,clip,error))return false;
            if(!clip.SourceAvatar.Guid.empty()) {GameSproutRuntime::HumanoidAvatarData avatar;if(!resources->LoadHumanoidAvatar(clip.SourceAvatar,avatar,error))return false;}
            else for(const auto& track:clip.Tracks)if(!paths.empty()&&!paths.count(track)){error="动画轨道与 Mask 骨架不兼容。";return false;}
            if(ref==layer.ReferenceClip) {
                if(layer.ReferenceTime>clip.Duration){error="叠加层参考时间超出动画时长。";return false;}
                std::size_t left=0;while(left+1<clip.Frames.size()&&clip.Frames[left+1].Time<layer.ReferenceTime)++left;
                const auto right=std::min(left+1,clip.Frames.size()-1);
                const float start=clip.Frames[left].Time,end=clip.Frames[right].Time;
                const float weight=end>start?(layer.ReferenceTime-start)/(end-start):0;
                for(std::size_t index=0;index<clip.Tracks.size();++index) {
                    const auto scale=clip.Frames[left].Poses[index].Scale*(1-weight)+clip.Frames[right].Poses[index].Scale*weight;
                    if(std::fabs(scale.X)<0.000001f||std::fabs(scale.Y)<0.000001f||std::fabs(scale.Z)<0.000001f){error="叠加参考姿态包含零缩放。";return false;}
                }
            }
        }
    }
    return true;
}
}
bool RuntimeAssets::ControllerPath(const GameSproutRuntime::ResourceReference& reference,std::string& path,std::string& error) const{
    path.clear();error.clear();
    if(!_Assets||!_Library||!_Project){error="资源服务尚未初始化。";return false;}
    const Guid guid(reference.Guid);IAssetMeta* meta=_Assets->QueryAsset(guid);
    IAsset* asset=_Library->QueryAsset(guid,reference.LocalID);
    if(!meta||meta->_ImporterType!=eAssetImporterType::AnimatorController||!asset||asset->GetAssetType()!=eAssetType::AnimatorController
        ||!AssetArtifactIO::RelativePath(meta->GetPath())){error="找不到有效的独立 Controller 资源。";return false;}
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));
    const QString canonicalRoot=root.canonicalPath();const QFileInfo file(root.filePath(QString::fromStdString(meta->GetPath())));
    if(canonicalRoot.isEmpty()||!file.isFile()||file.isSymLink()||!file.canonicalFilePath().startsWith(canonicalRoot+"/")){
        error="Controller 源文件不在项目 Assets 内。";return false;}
    path=file.absoluteFilePath().toStdString();return true;
}
bool RuntimeAssets::LoadAnimatorControllerSource(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AnimatorControllerData& data,std::string& json,std::string& error) const{
    data={};json.clear();std::string path;Json::Value document;
    if(!ControllerPath(reference,path,error)||!ReadSource(path,document,error)
        ||!AnimationDefinition::ReadController(document,false,data,error))return false;
    json=AssetArtifactIO::JsonText(document);return true;
}
bool RuntimeAssets::SaveAnimatorControllerSource(const GameSproutRuntime::ResourceReference& reference,
    const GameSproutRuntime::AnimatorControllerData& data,const std::string& expectedJson,std::string& error){
    std::string path;Json::Value expected,current;
    if(!ControllerPath(reference,path,error)||!ReadSource(path,current,error))return false;
    if(!AssetArtifactIO::Parse(expectedJson,expected)||expected!=current){error="Controller 已被外部修改，请重新加载后再保存。";return false;}
    const auto source=AnimationDefinition::ControllerJson(data,false);
    GameSproutRuntime::AnimatorControllerData verified;
    if(!AnimationDefinition::ReadController(source,false,verified,error)||!ValidClips(_Library,_Resources,verified,error))return false;
    if(!AssetArtifactIO::Write(QString::fromStdString(path),QByteArray::fromStdString(AssetArtifactIO::JsonText(source)+"\n"),error))return false;
    if(!_Assets->ReimportAsset(Guid(reference.Guid))){error="源文件已保存，但未能提交 Controller 导入。";return false;}
    return true;
}
bool RuntimeAssets::CreateAnimatorControllerAsset(const std::string& directory,const std::string& name,
    const GameSproutRuntime::ResourceReference& model,std::string& path,std::string& error){
    path.clear();error.clear();
    if(!_Resources||!_Project||!_Library){error="资源服务尚未初始化。";return false;}
    if(name.empty()||name=="."||name==".."||name.find_first_of("/\\:")!=std::string::npos||name.find('\0')!=std::string::npos
        ||(!directory.empty()&&!AssetArtifactIO::RelativePath(directory))){error="请输入有效的 Controller 名称和 Assets 相对目录。";return false;}
    GameSproutRuntime::ModelData modelData;
    if(!_Resources->LoadModel(model,modelData,error)||modelData.Animations.empty()){if(error.empty())error="模型没有可用动作。";return false;}
    GameSproutRuntime::AnimatorControllerData controller;
    controller.Layers.emplace_back();auto& base=controller.Layers.back();base.Name="Base Layer";
    std::set<std::string> names;
    int idle=-1,walk=-1,run=-1;
    for(const auto& ref:modelData.Animations){
        IAsset* asset=_Library->QueryAsset(Guid(ref.Guid),ref.LocalID);
        if(!asset||asset->GetAssetType()!=eAssetType::AnimationClip){error="请等待模型全部动作成功导入。";return false;}
        GameSproutRuntime::AnimatorState state;state.Name=asset->GetName();state.Clip=ref;
        if(!names.insert(state.Name).second){state.Name+="_"+std::to_string(ref.LocalID);names.insert(state.Name);}
        const int index=static_cast<int>(base.States.size());const auto lower=Lower(state.Name);
        if(idle<0&&lower.find("idle")!=std::string::npos)idle=index;
        if(walk<0&&lower.find("walk")!=std::string::npos)walk=index;
        if(run<0&&lower.find("run")!=std::string::npos)run=index;
        base.States.push_back(std::move(state));
    }
    if(idle>=0&&walk>=0&&run>=0){
        controller.Parameters.push_back({"Speed",GameSproutRuntime::AnimatorParameterType::Float,0});base.DefaultState=idle;
        const auto add=[&](int target,GameSproutRuntime::AnimatorCompare compare,double value){GameSproutRuntime::AnimatorTransition transition;transition.To=target;
            transition.Conditions.push_back({"Speed",compare,value});return transition;};
        base.Transitions.push_back(add(idle,GameSproutRuntime::AnimatorCompare::Less,0.1));
        auto walking=add(walk,GameSproutRuntime::AnimatorCompare::Greater,0.1);walking.Conditions.push_back({"Speed",GameSproutRuntime::AnimatorCompare::Less,2});base.Transitions.push_back(walking);
        base.Transitions.push_back(add(run,GameSproutRuntime::AnimatorCompare::Greater,2));
    }else{
        controller.Parameters.push_back({"State",GameSproutRuntime::AnimatorParameterType::Int,0});
        for(std::size_t index=0;index<base.States.size();++index){GameSproutRuntime::AnimatorTransition transition;transition.To=static_cast<std::uint32_t>(index);
            transition.Conditions.push_back({"State",GameSproutRuntime::AnimatorCompare::Equal,static_cast<double>(index)});base.Transitions.push_back(std::move(transition));}
    }
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));const QString canonicalRoot=root.canonicalPath();
    const QFileInfo parent(root.filePath(QString::fromStdString(directory)));
    if(canonicalRoot.isEmpty()||!parent.isDir()||(parent.canonicalFilePath()!=canonicalRoot&&!parent.canonicalFilePath().startsWith(canonicalRoot+"/"))){error="目标目录必须已经存在于 Assets 内。";return false;}
    const std::string filename=name+".controller";const QString target=QDir(parent.absoluteFilePath()).filePath(QString::fromStdString(filename));
    if(QFileInfo::exists(target+".meta")||QFileInfo(target+".meta").isSymLink()){error="Controller 的 Meta 已存在，不能覆盖。";return false;}
    QFile output(target);const QByteArray bytes=QByteArray::fromStdString(AssetArtifactIO::JsonText(AnimationDefinition::ControllerJson(controller,false))+"\n");
    if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error="Controller 已存在或无法创建："+output.errorString().toStdString();return false;}
    if(output.write(bytes)!=bytes.size()){error=output.errorString().toStdString();output.close();QFile::remove(target);return false;}
    output.close();path=root.relativeFilePath(target).toStdString();return true;
}

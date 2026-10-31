#include "RuntimeAssets.h"
#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "../Shared/AvatarMaskDefinition.h"
#include "../Shared/AssetArtifactIO.h"

namespace {
bool ReadMaskSource(const std::string& path,Json::Value& document,std::string& error) {
    QFile file(QString::fromStdString(path));
    if(!file.open(QIODevice::ReadOnly)||file.size()<0||file.size()>8*1024*1024){error="无法读取 Mask 源文件，或超过 8 MiB。";return false;}
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError||!AssetArtifactIO::Parse(bytes.toStdString(),document)){error="Mask 源文件不是有效 JSON。";return false;}
    return true;
}
}
bool RuntimeAssets::MaskPath(const GameSproutRuntime::ResourceReference& reference,std::string& path,std::string& error) const {
    path.clear();error.clear();
    if(!_Assets||!_Library||!_Project){error="资源服务尚未初始化。";return false;}
    const Guid guid(reference.Guid);IAssetMeta* meta=_Assets->QueryAsset(guid);IAsset* asset=_Library->QueryAsset(guid,reference.LocalID);
    if(!meta||meta->_ImporterType!=eAssetImporterType::AvatarMask||!asset||asset->GetAssetType()!=eAssetType::AvatarMask
        ||!AssetArtifactIO::RelativePath(meta->GetPath())){error="找不到有效的独立 AvatarMask 资源。";return false;}
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));
    const QString canonicalRoot=root.canonicalPath();const QFileInfo file(root.filePath(QString::fromStdString(meta->GetPath())));
    if(canonicalRoot.isEmpty()||!file.isFile()||file.isSymLink()||!file.canonicalFilePath().startsWith(canonicalRoot+"/")) {
        error="Mask 源文件不在项目 Assets 内。";return false;
    }
    path=file.absoluteFilePath().toStdString();return true;
}
bool RuntimeAssets::LoadAvatarMaskSource(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::AvatarMaskData& data,std::string& sourceSnapshot,std::string& error) const {
    data={};sourceSnapshot.clear();std::string path;Json::Value document;
    if(!MaskPath(reference,path,error)||!ReadMaskSource(path,document,error))return false;
    GameSproutRuntime::AvatarMaskData verified;GameSproutRuntime::SkeletonData skeleton;
    if(!AvatarMaskDefinition::Read(document,false,verified,error)||(verified.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!_Resources->LoadSkeleton(verified.Skeleton,skeleton,error))
        ||!AvatarMaskDefinition::Validate(verified,skeleton,error))return false;
    data=std::move(verified);sourceSnapshot=AssetArtifactIO::JsonText(document);return true;
}
bool RuntimeAssets::SaveAvatarMaskSource(const GameSproutRuntime::ResourceReference& reference,
    const GameSproutRuntime::AvatarMaskData& data,const std::string& expectedSourceSnapshot,std::string& error) {
    std::string path;Json::Value expected,current;
    if(!MaskPath(reference,path,error)||!ReadMaskSource(path,current,error))return false;
    if(!AssetArtifactIO::Parse(expectedSourceSnapshot,expected)||expected!=current){error="AvatarMask 已被外部修改，请重新加载后再保存。";return false;}
    const auto source=AvatarMaskDefinition::Write(data,false);
    GameSproutRuntime::AvatarMaskData verified;GameSproutRuntime::SkeletonData skeleton;
    if(!AvatarMaskDefinition::Read(source,false,verified,error)||(verified.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!_Resources->LoadSkeleton(verified.Skeleton,skeleton,error))
        ||!AvatarMaskDefinition::Validate(verified,skeleton,error))return false;
    if(!AssetArtifactIO::Write(QString::fromStdString(path),QByteArray::fromStdString(AssetArtifactIO::JsonText(source)+"\n"),error))return false;
    if(!_Assets->ReimportAsset(Guid(reference.Guid))){error="Mask 源文件已保存，但未能提交导入。";return false;}
    return true;
}
bool RuntimeAssets::CreateAvatarMaskSource(const std::string& directory,const std::string& name,
    const GameSproutRuntime::ResourceReference& skeletonOrModel,std::string& path,std::string& error) {
    path.clear();error.clear();
    if(!_Resources||!_Project||!_Library){error="资源服务尚未初始化。";return false;}
    if(name.empty()||name=="."||name==".."||name.find_first_of("/\\:")!=std::string::npos||name.find('\0')!=std::string::npos
        ||(!directory.empty()&&!AssetArtifactIO::RelativePath(directory))){error="请输入有效的 Mask 名称和 Assets 相对目录。";return false;}
    IAsset* asset=_Library->QueryAsset(Guid(skeletonOrModel.Guid),skeletonOrModel.LocalID);
    GameSproutRuntime::AvatarMaskData mask;mask.Skeleton=skeletonOrModel;mask.DefaultWeight=1;mask.RootMotionWeight=1;
    if(asset&&asset->GetAssetType()==eAssetType::Model) {
        GameSproutRuntime::ModelData model;if(!_Resources->LoadModel(skeletonOrModel,model,error))return false;mask.Skeleton=model.Skeleton;
        if(!model.Avatar.Guid.empty()){mask.Mode=GameSproutRuntime::AvatarMaskMode::Humanoid;mask.Skeleton={};}
    } else if(!asset||asset->GetAssetType()!=eAssetType::Skeleton){error="请选择带骨架的 Model 或 Skeleton 资源。";return false;}
    GameSproutRuntime::SkeletonData skeleton;
    if((mask.Mode==GameSproutRuntime::AvatarMaskMode::Transform&&!_Resources->LoadSkeleton(mask.Skeleton,skeleton,error))||!AvatarMaskDefinition::Validate(mask,skeleton,error))return false;
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));const QString canonicalRoot=root.canonicalPath();
    const QFileInfo parent(root.filePath(QString::fromStdString(directory)));
    if(canonicalRoot.isEmpty()||!parent.isDir()||(parent.canonicalFilePath()!=canonicalRoot&&!parent.canonicalFilePath().startsWith(canonicalRoot+"/"))){error="目标目录必须已经存在于 Assets 内。";return false;}
    const QString target=QDir(parent.absoluteFilePath()).filePath(QString::fromStdString(name+".mask"));
    if(QFileInfo::exists(target+".meta")||QFileInfo(target+".meta").isSymLink()){error="Mask 的 Meta 已存在，不能覆盖。";return false;}
    QFile output(target);const QByteArray bytes=QByteArray::fromStdString(AssetArtifactIO::JsonText(AvatarMaskDefinition::Write(mask,false))+"\n");
    if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error="Mask 已存在或无法创建："+output.errorString().toStdString();return false;}
    if(output.write(bytes)!=bytes.size()){error=output.errorString().toStdString();output.close();QFile::remove(target);return false;}
    output.close();path=root.relativeFilePath(target).toStdString();return true;
}

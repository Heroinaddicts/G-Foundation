#include "PhysicsAuthoring.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "../Shared/PhysicsMaterialDefinition.h"
#include "../Shared/AssetArtifactIO.h"
#include "../Shared/AssetSourceIO.h"

namespace {
bool ReadSource(const std::string& path,Json::Value& document,std::string& error) {
    QFile file(QString::fromStdString(path));
    if(!file.open(QIODevice::ReadOnly)||file.size()<0||file.size()>64*1024){error="无法读取物理材质，或文件超过 64 KiB。";return false;}
    const auto bytes=file.readAll();
    if(file.error()!=QFileDevice::NoError||!AssetArtifactIO::Parse(bytes.toStdString(),document)) {
        error="物理材质源文件不是有效 JSON。";return false;
    }
    return true;
}
}
bool PhysicsAuthoring::MaterialPath(const GameSproutRuntime::ResourceReference& reference,std::string& path,std::string& error) const {
    path.clear();error.clear();
    if(!_Assets||!_Library||!_Project){error="物理材质资源服务尚未就绪。";return false;}
    const Guid guid(reference.Guid);const auto* meta=_Assets->QueryAsset(guid);
    const auto* asset=reference.LocalID?_Library->QueryAsset(guid,reference.LocalID):nullptr;
    if(!guid.IsValid()||!meta||meta->_ImporterType!=eAssetImporterType::PhysicsMaterial||
       (reference.LocalID&&(!asset||asset->GetAssetType()!=eAssetType::PhysicsMaterial))||!AssetArtifactIO::RelativePath(meta->GetPath())) {
        error="引用不是有效的物理材质资源。";return false;
    }
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));
    const QString canonical=root.canonicalPath();const QFileInfo file(root.filePath(QString::fromStdString(meta->GetPath())));
    if(canonical.isEmpty()||!file.isFile()||file.isSymLink()||!file.canonicalFilePath().startsWith(canonical+"/")) {
        error="物理材质文件必须在项目 Assets 内。";return false;
    }
    path=file.absoluteFilePath().toStdString();return true;
}
bool PhysicsAuthoring::LoadPhysicsMaterialSource(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::PhysicsMaterialData& data,std::string& snapshot,std::string& error) const {
    data={};snapshot.clear();std::string path;Json::Value source;
    GameSproutRuntime::PhysicsMaterialData verified;
    if(!MaterialPath(reference,path,error)||!ReadSource(path,source,error)||!PhysicsMaterialDefinition::Read(source,verified,error))return false;
    data=verified;snapshot=AssetArtifactIO::JsonText(source);return true;
}
bool PhysicsAuthoring::SavePhysicsMaterialSource(const GameSproutRuntime::ResourceReference& reference,
    const GameSproutRuntime::PhysicsMaterialData& data,const std::string& snapshot,std::string& error) {
    std::string path;Json::Value expected,current;
    if(!PhysicsMaterialDefinition::Valid(data,error)||!MaterialPath(reference,path,error)||!ReadSource(path,current,error))return false;
    if(!AssetArtifactIO::Parse(snapshot,expected)||expected!=current){error="物理材质已被外部修改，请重新加载后保存。";return false;}
    const auto source=PhysicsMaterialDefinition::Write(data);
    if(!AssetArtifactIO::Write(QString::fromStdString(path),QByteArray::fromStdString(AssetSourceIO::JsonText(source)),error))return false;
    if(!_Assets->ReimportAsset(Guid(reference.Guid))){error="源文件已保存，但未能提交物理材质导入。";return false;}
    return true;
}
bool PhysicsAuthoring::CreatePhysicsMaterialAsset(const std::string& directory,const std::string& name,std::string& path,std::string& error) {
    path.clear();error.clear();
    if(!_Project){error="项目服务尚未就绪。";return false;}
    if(name.empty()||name=="."||name==".."||name.size()>240||name.find_first_of("/\\:")!=std::string::npos||
       name.find('\0')!=std::string::npos||(!directory.empty()&&!AssetArtifactIO::RelativePath(directory))) {
        error="请输入有效的物理材质名称及 Assets 相对目录。";return false;
    }
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));const QString canonical=root.canonicalPath();
    const QFileInfo parent(root.filePath(QString::fromStdString(directory)));
    if(canonical.isEmpty()||!parent.isDir()||(parent.canonicalFilePath()!=canonical&&!parent.canonicalFilePath().startsWith(canonical+"/"))) {
        error="目标目录必须已存在于 Assets 内。";return false;
    }
    const QString target=QDir(parent.absoluteFilePath()).filePath(QString::fromStdString(name+".physicmaterial"));
    if(QFileInfo::exists(target+".meta")||QFileInfo(target+".meta").isSymLink()){error="对应 Meta 已存在，不能覆盖。";return false;}
    QFile file(target);const QByteArray bytes=QByteArray::fromStdString(AssetSourceIO::JsonText(PhysicsMaterialDefinition::Write({})));
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error="文件已存在或无法创建："+file.errorString().toStdString();return false;}
    if(file.write(bytes)!=bytes.size()){error=file.errorString().toStdString();file.close();QFile::remove(target);return false;}
    file.close();path=root.relativeFilePath(target).toStdString();return true;
}

#include "ShaderAuthoring.h"
#include "ShaderTemplates.h"
#include "IProject.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "../Shared/MaterialDefinition.h"
#include "../Shared/AssetSourceIO.h"
#include <QThread>

bool ShaderAuthoring::CreateSource(const std::string& directory,const std::string& name,const char* extension,
    const std::string& source,std::string& path,std::string& error) {
    path.clear();error.clear();
    if(!_Project||QThread::currentThread()!=_OwnerThread){error="资源创建只能在编辑器主线程执行。";return false;}
    const QString filename=QString::fromStdString(name);
    if(name.empty()||name.size()>200||filename.trimmed()!=filename||name=="."||name==".."||
       name.find_first_of("/\\:\"<>|?*")!=std::string::npos||name.find('\0')!=std::string::npos||
       filename.endsWith('.')||(!directory.empty()&&!AssetArtifactIO::RelativePath(directory))) {
        error="请输入有效名称（无需扩展名）及 Assets 相对目录。";return false;
    }
    const QDir root(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));
    const QString canonical=root.canonicalPath();
    const QFileInfo parent(root.filePath(QString::fromStdString(directory)));
    if(canonical.isEmpty()||!parent.isDir()||parent.isSymLink()||
       (parent.canonicalFilePath()!=canonical&&!parent.canonicalFilePath().startsWith(canonical+"/"))) {
        error="目标目录必须已存在于项目 Assets 内。";return false;
    }
    const QString targetName=filename+QString::fromLatin1(extension);
    const QDir targetDirectory(parent.absoluteFilePath());
    for(const auto& existing:targetDirectory.entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System))
        if(existing.compare(targetName,Qt::CaseInsensitive)==0||existing.compare(targetName+".meta",Qt::CaseInsensitive)==0) {
            error="同名资源或 Meta 已存在，不能覆盖。";return false;
        }
    const QString target=targetDirectory.filePath(targetName);QFile file(target);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){error="无法创建资源："+file.errorString().toStdString();return false;}
    const QByteArray bytes=QByteArray::fromStdString(source);
    if(file.write(bytes)!=bytes.size()||!file.flush()) {
        error="资源写入失败："+file.errorString().toStdString();file.close();QFile::remove(target);return false;
    }
    file.close();path=root.relativeFilePath(target).toStdString();return true;
}
bool ShaderAuthoring::CreateUnlitShader(const std::string& directory,const std::string& name,std::string& path,std::string& error) {
    return CreateSource(directory,name,".shader",std::string(ShaderTemplates::Unlit),path,error);
}
bool ShaderAuthoring::CreateBuiltinMaterial(const std::string& directory,const std::string& name,
    BuiltinMaterialPreset preset,std::string& path,std::string& error) {
    path.clear();error.clear();
    if(preset!=BuiltinMaterialPreset::Preview&&preset!=BuiltinMaterialPreset::Lit){error="未知内置材质类型。";return false;}
    Json::Value document(Json::objectValue);document["Version"]=1;document["Name"]=name;
    document["Shader"]=preset==BuiltinMaterialPreset::Lit?"GameSprout/Lit":"GameSprout/Preview";
    const auto schema=MaterialDefinition::BuiltinSchema(document);
    if(!MaterialDefinition::NormalizeValues(schema,document,error))return false;
    return CreateSource(directory,name,".mat",AssetSourceIO::JsonText(document),path,error);
}
bool ShaderAuthoring::CreateMaterial(const std::string& directory,const std::string& name,
    const GameSproutRuntime::ResourceReference& selection,std::string& path,std::string& error) {
    path.clear();error.clear();
    if(!_Assets||!_Library||QThread::currentThread()!=_OwnerThread){error="材质资源服务尚未就绪。";return false;}
    Json::Value document(Json::objectValue);document["Version"]=1;document["Name"]=name;
    if(selection.Guid.empty()&&!selection.LocalID)document["Shader"]="GameSprout/Preview";
    else {
        const Guid guid(selection.Guid);const auto* meta=_Assets->QueryAsset(guid);
        if(!guid.IsValid()||!meta||meta->_ImporterType!=eAssetImporterType::Shader||meta->GetImportState()!=eAssetImportState::Ready) {
            error="选择的 Shader 尚未成功导入，无法创建材质。";return false;
        }
        uint64_t localID=selection.LocalID;
        if(!localID) {
            AssetImportManifest manifest;
            if(!_Library->GetManifest(guid,manifest)){error="选择的 Shader 缺少已发布产物。";return false;}
            for(const auto& asset:manifest.Assets)if(asset.Type==eAssetType::Shader) {
                if(localID){error="Shader 主资源不唯一。";return false;}localID=asset.LocalID;
            }
        }
        const auto* asset=localID?_Library->QueryAsset(guid,localID):nullptr;
        if(!asset||asset->GetAssetType()!=eAssetType::Shader){error="选择的资源不是可用的图形 Shader。";return false;}
        document["Shader"]["Guid"]=guid.GetGuidString();document["Shader"]["LocalID"]=Json::UInt64(localID);
    }
    Json::Value schema;
    if(!MaterialDefinition::ResolveSchema(_Library,document,schema,error)||!MaterialDefinition::NormalizeValues(schema,document,error))return false;
    return CreateSource(directory,name,".mat",AssetSourceIO::JsonText(document),path,error);
}

#include "PrefabStage.h"
#include "IProject.h"
#include "../Shared/PrefabDefinition.h"
#include "../Shared/AssetArtifactIO.h"
#include "../Shared/AssetSourceIO.h"
#include <QSaveFile>

namespace {
bool Resolve(IProject* project,const std::string& relative,QString& result,std::string& error) {
    if(!project||!AssetArtifactIO::RelativePath(relative)||QFileInfo(QString::fromStdString(relative)).suffix().compare("prefab",Qt::CaseInsensitive)!=0) {
        error="Prefab必须是Assets内的.prefab源文件。";return false;
    }
    const QFileInfo root(QString::fromStdString(project->GetProjectRoot()+"/Assets"));
    if(!root.isDir()||root.isSymLink()){error="Assets目录不可用。";return false;}
    const QString base=root.canonicalFilePath();QString current=base;
    for(const QString& part:QString::fromStdString(relative).split('/',Qt::SkipEmptyParts)) {
        current=QDir(current).filePath(part);
        if(QFileInfo(current).isSymLink()){error="Prefab路径不能经过符号链接。";return false;}
    }
    const QFileInfo file(current);
    if(!file.isFile()||!file.canonicalFilePath().startsWith(base+'/')){error="Prefab源已移除或路径越出Assets。";return false;}
    result=current;return true;
}
}
bool PrefabStage::ReadSource(const std::string& text,Json::Value& data,std::string& path,std::string& hash,std::string& error) const {
    error.clear();const Guid guid(text);
    const auto* meta=_Assets&&guid.IsValid()?_Assets->QueryAsset(guid):nullptr;
    if(!meta||meta->_ImporterType!=eAssetImporterType::Prefab){error="Prefab来源已移除或身份无效。";return false;}
    path=meta->GetPath();QString full;if(!Resolve(_Project,path,full,error))return false;
    QFile file(full);
    if(!file.open(QIODevice::ReadOnly)){error="无法读取Prefab源。";return false;}
    const auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError){error="读取Prefab源失败。";return false;}
    Json::Value source;
    if(!AssetArtifactIO::Parse(bytes.toStdString(),source)){error="Prefab源不是有效JSON。";return false;}
    if(!PrefabDefinition::Validate(source,data,error))return false;
    hash=AssetArtifactIO::Hash(bytes);return true;
}
bool PrefabStage::Capture(Json::Value& data,std::string& text,std::string& error) const {
    if(!_State.Active||!_Scenes->IsDocumentSessionActive()||_Scenes->IsEditingSuspendedDocument()) {
        error="Prefab Stage独立文档已经失效。";return false;
    }
    auto* scene=_Scenes->GetActiveScene();auto* root=_Scenes->GetDocumentContentRoot();
    if(!scene||scene->GetInstanceID()!=_State.SceneID||!root||root->GetInstanceID()!=_State.ContentRootObjectID||
       scene->GetChildCount()!=1||scene->GetChild(0)!=root) {
        error="Prefab Stage必须保留唯一内容根，不能保存根外的节点。";return false;
    }
    try {
        const auto annotations=_Scenes->GetSceneAnnotationsJson();Json::Value records;
        if(!AssetArtifactIO::Parse(annotations,records)||!records["PrefabInstances"].empty()) {
            error="本版Prefab Stage不支持Nested实例。";return false;
        }
        SceneSubtreeDocument snapshot;
        if(!_Scenes->CaptureObjectDocument(_State.ContentRootObjectID,snapshot,error))return false;
        if(!snapshot.ExternalObjectFileIDs.empty()){error="Prefab Stage不能保存内容根之外的场景引用。";return false;}
        Json::Value raw;
        if(!AssetArtifactIO::Parse(snapshot.DocumentJson,raw)){error="无法捕获Prefab舞台。";return false;}
        raw["Name"]=root->GetName();data=PrefabDefinition::Canonicalize(std::move(raw),true);
        Json::Value checked;
        if(!PrefabDefinition::Validate(PrefabDefinition::Source(data),checked,error))return false;
        text=AssetArtifactIO::JsonText(data);return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
bool PrefabStage::WriteSource(const Json::Value& data,const std::string& expected,std::string& hash,std::string& error) {
    Json::Value verified;const auto source=PrefabDefinition::Source(data);
    if(!PrefabDefinition::Validate(source,verified,error))return false;
    QString full;if(!Resolve(_Project,_State.Path,full,error))return false;
    const auto bytes=QByteArray::fromStdString(AssetSourceIO::JsonText(source));
    QSaveFile file(full);file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly)){error="无法写入Prefab源。";return false;}
    if(file.write(bytes)!=bytes.size()){error="写入Prefab源失败。";return false;}
    Json::Value current;std::string path,currentHash;
    if(!ReadSource(_State.Guid,current,path,currentHash,error)||path!=_State.Path||currentHash!=expected) {
        if(error.empty())error="Prefab源在保存期间被外部修改，未覆盖。";file.cancelWriting();return false;
    }
    if(!file.commit()){error="提交Prefab源失败；原源文件保留。";return false;}
    hash=AssetArtifactIO::Hash(bytes);return true;
}

#include "PrefabManager.h"
#include "PrefabRecords.h"
#include "../Shared/AssetSourceIO.h"
#include "ScriptReferenceDocument.h"
#include <QSaveFile>
#include <QRegularExpression>

namespace {
// Source assets cannot retain scene-local identities. Clear only authored
// object-reference slots in this copy; never mutate the captured Runtime graph.
void ClearExternalObjectReferences(Json::Value& document) {
    std::set<uint64_t> local,localComponents;
    for(const auto& node:document["Objects"]) {
        local.insert(node["ID"].asUInt64());
        for(const auto& component:node["Components"])if(component.isMember("ID"))localComponents.insert(component["ID"].asUInt64());
    }
    for(auto& node:document["Objects"])for(auto& component:node["Components"]) {
        const auto type=component["Type"].asString();
        if(type=="GameSproutRuntime.ScriptComponent") {
            ScriptReferenceDocument::Fields(component,[&](Json::Value& field,bool isComponent) {
                // Assigned FileID 0 is the external scene root, not None.
                if(!(isComponent?localComponents:local).count(field["Value"][isComponent?"ComponentFileID":"ObjectFileID"].asUInt64()))
                    field["Value"]=Json::Value();
            });
        }else if(type=="GameSproutRuntime.Animator") {
            for(auto& binding:component["Bindings"])
                if(!local.count(binding["ObjectFileID"].asUInt64()))binding["ObjectFileID"]=Json::UInt64(0);
        }else if(type=="GameSproutRuntime.SkinnedMeshRenderer") {
            if(!local.count(component["AnimatorObjectFileID"].asUInt64()))component["AnimatorObjectFileID"]=Json::UInt64(0);
        }
    }
}
QString Path(IProject* project,const std::string& path,bool writing,std::string& error) {
    if(!project||!AssetArtifactIO::RelativePath(path)||QFileInfo(QString::fromStdString(path)).suffix().compare("prefab",Qt::CaseInsensitive)!=0){error="路径必须是Assets下的.prefab文件。";return {};}
    const QDir root(QString::fromStdString(project->GetProjectRoot()+"/Assets"));
    const QString base=QFileInfo(root.absolutePath()).canonicalFilePath();
    const QString full=root.absoluteFilePath(QString::fromStdString(path));
    const QFileInfo parent(QFileInfo(full).absolutePath());
    if(base.isEmpty()||parent.canonicalFilePath().isEmpty()||
       (parent.canonicalFilePath()!=base&&!parent.canonicalFilePath().startsWith(base+"/"))||QFileInfo(full).isSymLink()) {
        error="Prefab路径越出Assets，或目录不存在/包含符号链接。";return {};
    }
    QString cursor=QFileInfo(full).absolutePath();
    while(cursor!=base&&cursor.size()>=base.size()) {if(QFileInfo(cursor).isSymLink()){error="Prefab目录不允许符号链接。";return {};}cursor=QFileInfo(cursor).absolutePath();}
    if(writing&&QFileInfo::exists(full)){error="Prefab已经存在，不会覆盖。";return {};}
    return full;
}
}
bool PrefabManager::Load(const GameSproutRuntime::ResourceReference& reference,Json::Value& data,
    std::string& path,std::string& hash,bool artifact,std::string& error) const {
    error.clear();const Guid guid(reference.Guid);
    auto* meta=_Assets&&guid.IsValid()?_Assets->QueryAsset(guid):nullptr;
    if(!meta||meta->_ImporterType!=eAssetImporterType::Prefab||!reference.LocalID){error="Prefab来源缺失或身份无效。";return false;}
    path=meta->GetPath();const auto full=Path(_Project,path,false,error);if(full.isEmpty())return false;
    QFile file(full);if(!file.open(QIODevice::ReadOnly)){error="无法读取Prefab源文件。";return false;}
    const auto bytes=file.readAll();if(file.error()!=QFileDevice::NoError){error="读取Prefab源文件失败。";return false;}
    hash=AssetArtifactIO::Hash(bytes);Json::Value source;
    if(!AssetArtifactIO::Parse(bytes.toStdString(),source)||!PrefabDefinition::Validate(source,data,error)){if(error.empty())error="Prefab格式无效。";return false;}
    if(artifact) {
        AssetImportManifest manifest;
        if(!_Library||!_Library->GetManifest(guid,manifest)||manifest.SourceHash!=hash){error="Prefab正在预处理，请导入完成后重试。";return false;}
        auto* asset=_Library->QueryAsset(guid,reference.LocalID);
        if(!asset||asset->GetAssetType()!=eAssetType::Prefab){error="找不到Prefab预处理产物。";return false;}
        QFile prepared(QString::fromStdString(asset->GetArtifactPath()));
        if(!prepared.open(QIODevice::ReadOnly)){error="无法读取Prefab预处理产物。";return false;}
        const auto content=prepared.readAll();
        if(prepared.error()!=QFileDevice::NoError){error="读取Prefab预处理产物失败。";return false;}
        std::vector<uint8_t> binary(reinterpret_cast<const uint8_t*>(content.constData()),reinterpret_cast<const uint8_t*>(content.constData())+content.size());
        Json::Value decoded;
        if(!PrefabDocument::Decode(binary,decoded,error))return false;
        if(decoded!=data){error="Prefab预处理产物与源快照不一致。";return false;}
        data=std::move(decoded);
    }
    return true;
}
bool PrefabManager::SavePrefab(uint64_t objectID,const std::string& path,std::string& error) {
    return SaveSource(objectID,path,false,error);
}
bool PrefabManager::CreatePrefab(uint64_t objectID,const std::string& path,std::string& error) {
    return SaveSource(objectID,path,true,error);
}
bool PrefabManager::CreatePrefabInDirectory(uint64_t objectID,const std::string& directory,std::string& output,std::string& error) {
    if(!Ready(error))return false;
    if(!directory.empty()&&!AssetArtifactIO::RelativePath(directory)){error="目标目录必须在Assets内。";return false;}
    auto* object=_Runtime->QueryGameObject(objectID);
    if(!object||object==_Scenes->GetActiveScene()||object->GetScene()!=_Scenes->GetActiveScene()){error="请选择当前场景中的普通对象。";return false;}
    QString name=QString::fromStdString(object->GetName()).left(120);
    for(qsizetype index=0;index<name.size();++index)
        if(name[index].unicode()<32||QStringLiteral("<>:\"/\\|?*").contains(name[index]))name[index]=QChar('_');
    name=name.trimmed();while(name.endsWith('.')||name.endsWith(' '))name.chop(1);
    if(name.isEmpty())name=QStringLiteral("GameObject");
    if(QRegularExpression(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),QRegularExpression::CaseInsensitiveOption).match(name).hasMatch())name.prepend('_');
    const QDir assets(QString::fromStdString(_Project->GetProjectRoot()+"/Assets"));
    for(uint64_t suffix=0;;++suffix) {
        const auto filename=name+(suffix?QStringLiteral(" ")+QString::number(suffix):QString())+QStringLiteral(".prefab");
        const auto relative=directory.empty()?filename:QDir(QString::fromStdString(directory)).filePath(filename);
        if(QFileInfo::exists(assets.filePath(relative)))continue;
        const auto path=relative.toStdString();
        if(!CreatePrefab(objectID,path,error))return false;
        output=path;return true;
    }
}
bool PrefabManager::SaveSource(uint64_t objectID,const std::string& path,bool connect,std::string& error) {
    if(!Ready(error))return false;const auto full=Path(_Project,path,true,error);if(full.isEmpty())return false;
    if(connect) {
        Json::Value existing;std::string ignored;
        if(FindRecord(objectID,existing,ignored)) {
            auto* root=_Scenes->QueryObjectByFileID(existing["RootFileID"].asUInt64());
            if(!root||root->GetInstanceID()!=objectID){error="不能从Prefab实例内部创建连接；请选择实例根。";return false;}
        }
    }
    SceneSubtreeDocument snapshot;if(!_Scenes->CaptureObjectDocument(objectID,snapshot,error))return false;
    if(connect&&!ConnectionAllowed(objectID,snapshot,error))return false;
    try {
        Json::Value data;
        if(!AssetArtifactIO::Parse(snapshot.DocumentJson,data)){error="无法读取节点快照。";return false;}
        ClearExternalObjectReferences(data);
        std::map<uint64_t,uint64_t> canonicalIDs;
        data=PrefabDefinition::Canonicalize(std::move(data),false,&canonicalIDs);
        data["Name"]=QFileInfo(full).completeBaseName().toStdString();
        for(auto& node:data["Objects"])if(node["ID"].asUInt64()==PrefabDefinition::RootID(data))node["Name"]=data["Name"];
        Json::Value verified;const auto source=PrefabDefinition::Source(data);
        if(!PrefabDefinition::Validate(source,verified,error))return false;
        const auto bytes=QByteArray::fromStdString(AssetSourceIO::JsonText(source));
        PendingCreation pending;
        if(connect) {
            pending.SceneID=_Scenes->GetActiveScene()->GetInstanceID();pending.SceneEpoch=_SceneEpoch;
            pending.ObjectID=objectID;pending.RootFileID=_Scenes->GetObjectFileID(objectID);pending.Path=path;
            pending.ProjectRoot=_Project->GetProjectRoot();pending.BaselineJson=AssetArtifactIO::JsonText(data);
            pending.SourceHash=AssetArtifactIO::Hash(bytes);Json::Value mappings(Json::arrayValue);
            for(const auto& identity:snapshot.ObjectIDs) {
                Json::Value item;item["SourceID"]=Json::UInt64(canonicalIDs.at(identity.SceneFileID));
                item["SceneID"]=Json::UInt64(identity.SceneFileID);item["Component"]=identity.Component;mappings.append(std::move(item));
            }
            pending.MappingsJson=AssetArtifactIO::JsonText(mappings);
        }
        QSaveFile file(full);file.setDirectWriteFallback(false);
        if(QFileInfo::exists(full)||!file.open(QIODevice::WriteOnly)){error="无法创建Prefab，目标已经存在或不可写。";return false;}
        if(connect)_Pending.insert_or_assign(path,pending);
        if(file.write(bytes)!=bytes.size()||QFileInfo::exists(full)||!file.commit()){
            if(connect)_Pending.erase(path);error="写入Prefab失败或目标已被其他操作创建。";return false;
        }
        if(connect)for(auto it=_Pending.begin();it!=_Pending.end();) {
            if(it->first!=path&&it->second.SceneID==pending.SceneID&&it->second.ObjectID==objectID)it=_Pending.erase(it);else ++it;
        }
        // AssetsManager owns .meta and Pipeline tasks. No direct DB writes here.
        QueueNotify();return true;
    }catch(const std::exception& exception){if(connect)_Pending.erase(path);error=exception.what();return false;}
}

#include "AssetImportJob.h"
#include <QStringDecoder>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QProcess>

namespace {
bool References(AssetImportJob & job,const Json::Value & value) {
    if (value.isObject()) {
        if (value.isMember("Guid") && value.isMember("LocalID")) {
            if (!value["Guid"].isString() || !Guid(value["Guid"].asString()).IsValid()
                || !value["LocalID"].isUInt64() || value["LocalID"].asUInt64()==0) return job.Fail("Invalid resource reference");
            const std::string guid=value["Guid"].asString();
            if (guid!=job.Request.GuidString) {
                const auto path=job.Request.SourcePaths.find(guid);
                if (path==job.Request.SourcePaths.end()) return job.Fail("Referenced asset GUID is missing: "+guid);
                if (!job.AddDependency(path->second)) return false;
            }
        }
        for (const auto & child : value) if (!References(job,child)) return false;
    } else if (value.isArray()) for (const auto & child : value) if (!References(job,child)) return false;
    return true;
}

bool Decode(AssetImportJob & job,QByteArray bytes,QByteArray & output) {
    const std::string encoding=job.Settings.get("Encoding","Auto").asString();
    if (encoding=="Binary") {output=bytes;return true;}
    QStringConverter::Encoding format=QStringConverter::Utf8;
    if (encoding=="UTF16LE") format=QStringConverter::Utf16LE;
    else if (encoding=="UTF16BE") format=QStringConverter::Utf16BE;
    else if (encoding=="Auto") {
        if (bytes.startsWith("\xff\xfe")) format=QStringConverter::Utf16LE;
        else if (bytes.startsWith("\xfe\xff")) format=QStringConverter::Utf16BE;
    } else if (encoding!="UTF8") return job.Fail("Unsupported text encoding");
    QStringDecoder decoder(format);
    QString text=decoder(bytes);
    if (decoder.hasError()) return job.Fail("Invalid text encoding");
    if (job.Settings.get("StripByteOrderMark",true).asBool() && !text.isEmpty() && text.front()==QChar(0xfeff)) text.remove(0,1);
    if (job.Settings.get("NormalizeLineEndings",true).asBool()) text.replace("\r\n","\n").replace('\r','\n');
    output=text.toUtf8();return true;
}

bool Includes(AssetImportJob & job,const std::string & path,const QByteArray & source,std::set<std::string> & seen) {
    const QRegularExpression expression(R"include(^\s*#\s*include\s*"([^"]+)"\s*$)include",QRegularExpression::MultilineOption);
    auto matches=expression.globalMatch(QString::fromUtf8(source));
    while (matches.hasNext()) {
        const QString dependency=QDir(QFileInfo(QString::fromStdString(path)).path()).filePath(matches.next().captured(1));
        const std::string relative=QDir::cleanPath(dependency).toStdString();
        if (!seen.insert(relative).second) continue;
        QByteArray bytes;
        if (!job.AddDependency(relative,&bytes) || !Includes(job,relative,bytes,seen)) return false;
    }
    return true;
}
}

bool ImportDocument(AssetImportJob & job) {
    const std::string name=QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString();
    const QString extension=QFileInfo(QString::fromStdString(job.Request.SourcePath)).suffix().toLower();
    Json::Value metadata;
    metadata["Settings"]=job.Settings;
    eAssetType type=eAssetType::Unknown;
    switch (job.Request.Importer) {
    case eAssetImporterType::Text: {
        QByteArray bytes;
        if (!Decode(job,job.SourceBytes,bytes)) return false;
        metadata["Format"]=job.Settings.get("Encoding","Auto")=="Binary" ? "Binary" : "UTF8";
        return job.Emit(eAssetType::Text,"main",name,metadata,bytes);
    }
    case eAssetImporterType::ShaderInclude: {
        QByteArray bytes;
        if (!Decode(job,job.SourceBytes,bytes)) return false;
        std::set<std::string> seen{job.Request.SourcePath};
        if (!Includes(job,job.Request.SourcePath,bytes,seen)) return false;
        metadata["Format"]="ShaderIncludeSource";
        return job.Emit(eAssetType::ShaderInclude,"main",name,metadata,bytes);
    }
    case eAssetImporterType::Script:
        return job.Fail("Script compilation requires the selected scripting runtime/compiler; source is retained in Assets");
    case eAssetImporterType::Shader:
    case eAssetImporterType::ComputeShader:
        return ImportShader(job);
    case eAssetImporterType::Mesh: type=eAssetType::Mesh;break;
    case eAssetImporterType::Material:type=eAssetType::Material;break;
    case eAssetImporterType::Scene:type=eAssetType::Scene;break;
    case eAssetImporterType::AnimationClip:type=eAssetType::AnimationClip;break;
    case eAssetImporterType::AnimatorController:type=eAssetType::AnimatorController;break;
    case eAssetImporterType::AnimatorOverrideController:type=eAssetType::AnimatorOverrideController;break;
    case eAssetImporterType::AvatarMask:type=eAssetType::AvatarMask;break;
    case eAssetImporterType::AudioMixer:type=eAssetType::AudioMixer;break;
    case eAssetImporterType::PhysicsMaterial:type=eAssetType::PhysicsMaterial;break;
    case eAssetImporterType::PhysicsMaterial2D:type=eAssetType::PhysicsMaterial2D;break;
    case eAssetImporterType::RenderTexture:type=eAssetType::RenderTexture;break;
    case eAssetImporterType::Cubemap:type=eAssetType::Cubemap;break;
    case eAssetImporterType::SpriteAtlas:type=eAssetType::SpriteAtlas;break;
    case eAssetImporterType::TerrainData:type=eAssetType::TerrainData;break;
    case eAssetImporterType::TerrainLayer:type=eAssetType::TerrainLayer;break;
    case eAssetImporterType::ShaderVariantCollection:type=eAssetType::ShaderVariantCollection;break;
    case eAssetImporterType::LightingSettings:type=eAssetType::LightingSettings;break;
    case eAssetImporterType::LightmapParameters:type=eAssetType::LightmapParameters;break;
    case eAssetImporterType::DataAsset:
        metadata["Format"]="BinaryData";
        return job.Emit(eAssetType::DataAsset,"main",name,metadata,job.SourceBytes);
    default:return job.Fail("No importer registered for this source type");
    }
    Json::Value document;
    if (!AssetArtifactIO::Parse(job.SourceBytes.toStdString(),document) || !document.isObject()) return job.Fail("Native resource must be a JSON object in GameSprout format");
    if (!document.isMember("Version") || document["Version"]!=1) return job.Fail("Native resource Version must be 1");
    if (!References(job,document)) return false;
    // The current native format has one main object. Preserve an explicitly assigned ID.
    const Json::Value requested = job.Settings.get("MainObjectID", Json::UInt64(0));
    if (!requested.isUInt64()) return job.Fail("MainObjectID must be an unsigned integer");
    const uint64_t mainID = requested.asUInt64();
    if (mainID != 0) {
        const std::string key = std::to_string(static_cast<uint16_t>(type)) + ":main";
        const auto existing = job.IDs.find(key);
        if (existing != job.IDs.end() && existing->second != mainID)
            return job.Fail("MainObjectID conflicts with the persisted resource identity");
        if (existing == job.IDs.end()) {
            if (!job.UsedIDs.insert(mainID).second) return job.Fail("MainObjectID is already assigned to another resource");
            job.IDs.emplace(key, mainID);
            Json::Value identity;
            identity["LocalID"] = Json::UInt64(mainID); identity["Type"] = uint16_t(type);
            identity["SourceKey"] = "main"; identity["Name"] = name;
            job.Identities.append(identity);
        }
    }
    metadata["Format"]="GameSproutDocument";metadata["Data"]=document;
    return job.Emit(type,"main",name,metadata);
}

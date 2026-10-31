#include "AssetsManager.h"
#include "MaterialAssetFactory.hpp"
#include "../Shared/MaterialDefinition.h"
#include <QTemporaryFile>

namespace {
bool MaterialPath(const std::string & assetsRoot, const std::string & relativePath, bool mustExist,
    QString & path, std::string & error) {
    if (!AssetArtifactIO::RelativePath(relativePath)) {
        error = "材质路径必须是相对 Assets 的有效路径。";
        return false;
    }
    const QString root = QDir(QString::fromStdString(assetsRoot)).canonicalPath();
    if (root.isEmpty()) { error = "项目 Assets 目录不存在。"; return false; }
    const QFileInfo target(QDir(root).filePath(QString::fromStdString(relativePath)));
    const QString parent = target.dir().canonicalPath();
    if (parent.isEmpty() || (parent != root && !parent.startsWith(root + "/"))
        || parent != target.dir().absolutePath() || target.isSymLink()
        || target.suffix().compare("mat", Qt::CaseInsensitive) != 0
        || (mustExist && (!target.isFile() || !target.isReadable()))) {
        error = "材质必须位于 Assets 内已有的真实目录中，且后缀为 .mat。";
        return false;
    }
    path = QDir(parent).filePath(target.fileName());
    return true;
}

// 在目标目录写完整临时文件，再用不覆盖目标的 rename 发布。
bool WriteNewFile(const QString & path, const QByteArray & bytes, std::string & error) {
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) {
        error = "目标文件已经存在，不能覆盖：" + path.toStdString();
        return false;
    }
    QTemporaryFile file(QFileInfo(path).dir().filePath(".gamesprout-material-XXXXXX.tmp"));
    if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) {
        error = "无法写入新材质：" + file.errorString().toStdString();
        return false;
    }
    file.close();
    if (!file.rename(path)) {
        error = "无法创建目标文件（可能已经存在）：" + path.toStdString();
        return false;
    }
    file.setAutoRemove(false);
    return true;
}

bool MaterialDocument(const std::string & text, Json::Value & document, std::string & error) {
    if (text.size() > static_cast<std::size_t>(MaterialDefinition::MaxDocumentBytes)
        || !AssetArtifactIO::Parse(text, document) || !document.isObject() || document["Version"] != 1
        || document.isMember("ExternalMaterial")) {
        error = "材质源文件必须是 Version 为 1 的独立材质 JSON 文档。";
        return false;
    }
    return true;
}

bool TextureReferences(ILibraryManager * library, const Json::Value & schema,
    const Json::Value & document, std::string & error) {
    for (const auto & property : schema["Properties"]) {
        if (property["Type"] != "Texture") continue;
        const Json::Value & reference = document[property["Name"].asString()];
        if (reference.isNull()) continue;
        IAsset * resource = library->QueryAsset(Guid(reference["Guid"].asString()), reference["LocalID"].asUInt64());
        if (!resource || resource->GetAssetType() != eAssetType::Texture) {
            error = "材质贴图引用不存在或类型错误：" + property["Name"].asString();
            return false;
        }
    }
    return true;
}

bool AuthoringShader(IAssetsManager * assets, const std::string & assetsRoot,
    const Json::Value & document, const Json::Value & publishedSchema, std::string & error) {
    if (!document["Shader"].isObject()) return true;
    IAssetMeta * meta = assets->QueryAsset(Guid(document["Shader"]["Guid"].asString()));
    const QString root = QDir(QString::fromStdString(assetsRoot)).canonicalPath();
    if (!meta || meta->_ImporterType != eAssetImporterType::Shader || root.isEmpty()
        || !AssetArtifactIO::RelativePath(meta->GetPath())) {
        error = "Shader 源资源不存在，请等待资源刷新后重新编辑材质。";
        return false;
    }
    const QFileInfo source(QDir(root).filePath(QString::fromStdString(meta->GetPath())));
    QFile file(source.absoluteFilePath());
    constexpr qint64 maxShaderSourceBytes = 2 * 1024 * 1024;
    if (!source.isFile() || source.isSymLink() || !source.canonicalFilePath().startsWith(root + "/")
        || !file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > maxShaderSourceBytes) {
        error = "无法读取 Assets 内的 Shader 源文件，或文件超过 2 MiB 编辑检查上限。";
        return false;
    }
    const QByteArray bytes = file.read(maxShaderSourceBytes + 1);
    Json::Value sourceSchema;
    std::string schemaError;
    if (file.error() != QFileDevice::NoError || !file.atEnd() || bytes.size() > maxShaderSourceBytes
        || !MaterialDefinition::ParseShaderSchema(bytes, sourceSchema, schemaError)) {
        error = "Shader 参数声明无效，请先修复并完成 Shader 导入：" + schemaError;
        return false;
    }
    if (sourceSchema != publishedSchema) {
        error = "Shader 参数定义已修改，请等待 Shader 成功导入并刷新材质面板后再保存。草稿已保留。";
        return false;
    }
    return true;
}
}

bool AssetsManager::LoadMaterialSource(const Guid & guid, std::string & json, std::string & error) const {
    json.clear(); error.clear();
    IAssetMeta * meta = QueryAsset(guid);
    if (!meta || meta->_ImporterType != eAssetImporterType::Material || _ConflictingGuids.count(guid.GetGuidString())) {
        error = "独立材质不存在或 GUID 冲突。";
        return false;
    }
    QString path;
    if (!MaterialPath(_AssetsRoot, meta->GetPath(), true, path, error)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString().toStdString(); return false; }
    if (file.size() > MaterialDefinition::MaxDocumentBytes) { error = "材质源文件超过 16 MiB 大小限制。"; return false; }
    const QByteArray bytes = file.read(MaterialDefinition::MaxDocumentBytes + 1);
    Json::Value document;
    if (file.error() != QFileDevice::NoError) { error = file.errorString().toStdString(); return false; }
    if (!MaterialDocument(bytes.toStdString(), document, error)) return false;
    json = AssetArtifactIO::JsonText(document);
    return true;
}

bool AssetsManager::SaveMaterialSource(const Guid & guid, const std::string & json,
    const std::string & expectedJson, std::string & error) {
    error.clear();
    if (!_SourceDatabaseReady || !_Pipeline || !_Library) { error = "资源数据库尚未就绪。"; return false; }
    std::string currentText;
    if (!LoadMaterialSource(guid, currentText, error)) return false;
    Json::Value current, expected, document, schema;
    if (!MaterialDocument(expectedJson, expected, error) || !MaterialDocument(json, document, error)
        || !MaterialDocument(currentText, current, error)) return false;
    if (current != expected) { error = "材质源文件已被外部修改，请恢复后重新编辑。"; return false; }
    if (!MaterialDefinition::ResolveSchema(_Library, document, schema, error)
        || !AuthoringShader(this, _AssetsRoot, document, schema, error)
        || !MaterialDefinition::NormalizeValues(schema, document, error)
        || !TextureReferences(_Library, schema, document, error)) return false;
    if (document == current) return true;
    IAssetMeta * meta = QueryAsset(guid);
    QString path;
    if (!meta || !MaterialPath(_AssetsRoot, meta->GetPath(), true, path, error)) return false;
    const std::string savedText = AssetArtifactIO::JsonText(document) + "\n";
    if (savedText.size() > static_cast<std::size_t>(MaterialDefinition::MaxDocumentBytes)) {
        error = "材质源文件超过 16 MiB 大小限制。";
        return false;
    }
    if (!AssetArtifactIO::Write(path, QByteArray::fromStdString(savedText), error)) return false;
    ImportAsset(meta, true);
    ImportDependents(meta->GetPath());
    return true;
}

bool AssetsManager::ExtractMaterial(const Guid & guid, uint64_t localID, const std::string & relativeMatPath,
    std::string & outputGuid, std::string & error) {
    outputGuid.clear(); error.clear();
    auto * sourceMeta = static_cast<AssetMeta *>(QueryAsset(guid));
    if (!sourceMeta || sourceMeta->_ImporterType != eAssetImporterType::Model || !_SourceDatabaseReady
        || !_Library || !_Pipeline || !g_IGuidCreator || _ConflictingGuids.count(guid.GetGuidString())) {
        error = "只能从已登记的模型中提取材质。";
        return false;
    }
    AssetImportManifest manifest;
    if (!_Library->GetManifest(guid, manifest)) { error = "模型尚未成功导入。"; return false; }
    std::string sourceKey, materialName;
    for (const auto & asset : manifest.Assets) if (asset.LocalID == localID && asset.Type == eAssetType::Material) {
        sourceKey = asset.SourceKey;
        materialName = asset.Name;
        break;
    }
    if (sourceKey.empty()) { error = "选择的子资源不是有效材质。"; return false; }
    QString path;
    if (!MaterialPath(_AssetsRoot, relativeMatPath, false, path, error)) return false;
    const QString sidecar = path + ".meta";
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink() || QFileInfo::exists(sidecar)
        || QFileInfo(sidecar).isSymLink() || _AssetGuidsByPath.count(relativeMatPath)) {
        error = "目标材质或 Meta 已经存在，不能覆盖。";
        return false;
    }
    Json::Value document, schema, sourceValue;
    std::string sourceSnapshot;
    if (!sourceMeta->SerializeMeta(sourceSnapshot) || !AssetArtifactIO::Parse(sourceSnapshot, sourceValue)) {
        error = "无法读取模型导入设置。";
        return false;
    }
    const QDir assetsDirectory(QString::fromStdString(_AssetsRoot));
    const QFileInfo sourceFile(assetsDirectory.filePath(QString::fromStdString(sourceMeta->GetPath())));
    const QFileInfo sourceSidecar(sourceFile.absoluteFilePath() + ".meta");
    QFile sourceMetaFile(sourceSidecar.absoluteFilePath());
    Json::Value diskMeta;
    if (!sourceFile.isFile() || sourceFile.isSymLink() || sourceSidecar.isSymLink()
        || !sourceFile.canonicalFilePath().startsWith(assetsDirectory.canonicalPath() + "/")
        || !sourceMetaFile.open(QIODevice::ReadOnly)
        || sourceMetaFile.size() > MaterialDefinition::MaxDocumentBytes
        || !AssetArtifactIO::Parse(sourceMetaFile.read(MaterialDefinition::MaxDocumentBytes + 1).toStdString(), diskMeta)
        || sourceMetaFile.error() != QFileDevice::NoError || diskMeta != sourceValue) {
        error = "模型或 Meta 已被外部修改，请等待资源刷新后重新提取。";
        return false;
    }
    // 随后 ApplySettings 会原子替换此文件；Windows 上先释放读取句柄。
    sourceMetaFile.close();
    if (sourceValue["ImportSettings"]["MaterialRemaps"].isMember(sourceKey)) {
        error = "该材质已映射到独立材质，请打开对应的外部材质。";
        return false;
    }
    if (!MaterialDefinition::ReadPreparedMaterial(_Library, guid, localID, document, error)) return false;
    if (document.isMember("ExternalMaterial")) { error = "该材质已经提取。"; return false; }
    document["Version"] = 1;
    document["Name"] = materialName;
    if (!MaterialDefinition::ResolveSchema(_Library, document, schema, error)
        || !AuthoringShader(this, _AssetsRoot, document, schema, error)
        || !MaterialDefinition::NormalizeValues(schema, document, error)
        || !TextureReferences(_Library, schema, document, error)) return false;

    const Guid newGuid = g_IGuidCreator->CreateGuid();
    if (!newGuid.IsValid() || _Assets.count(newGuid.GetGuidString())) { error = "无法创建材质 GUID。"; return false; }
    MaterialMeta * extracted = new MaterialMeta(newGuid, relativeMatPath);
    auto * settings = static_cast<IMaterialImportSettings *>(extracted->GetImportSettings());
    settings->SetMainObjectID(localID);
    Json::Value targetMeta;
    if (!extracted->ConvertToJson(targetMeta)) { delete extracted; error = "无法生成材质 Meta。"; return false; }
    // 提前保存主资源身份，模型和 .mat 可并发导入，映射无需等待产物发布。
    Json::Value identity;
    identity["LocalID"] = Json::UInt64(localID);
    identity["Type"] = static_cast<uint16_t>(eAssetType::Material);
    identity["SourceKey"] = "main";
    identity["Name"] = materialName;
    targetMeta["SubAssets"].append(identity);
    if (!extracted->ConvertFromJson(targetMeta)) { delete extracted; error = "无法设置材质主资源身份。"; return false; }
    const std::string savedText = AssetArtifactIO::JsonText(document) + "\n";
    if (savedText.size() > static_cast<std::size_t>(MaterialDefinition::MaxDocumentBytes)) {
        delete extracted; error = "材质源文件超过 16 MiB 大小限制。"; return false;
    }
    if (!WriteNewFile(path, QByteArray::fromStdString(savedText), error)) {
        delete extracted;
        return false;
    }
    if (!WriteNewFile(sidecar, QByteArray::fromStdString(AssetArtifactIO::JsonText(targetMeta) + "\n"), error)) {
        QFile::remove(path);
        delete extracted;
        return false;
    }
    Json::Value remapped = sourceValue["ImportSettings"];
    remapped["MaterialRemaps"][sourceKey]["Guid"] = newGuid.GetGuidString();
    remapped["MaterialRemaps"][sourceKey]["LocalID"] = Json::UInt64(localID);
    if (!sourceMeta->ApplySettings(remapped)) {
        QFile::remove(sidecar); QFile::remove(path); delete extracted;
        error = "模型材质映射保存失败，提取已撤销。";
        return false;
    }
    StoreAssetMeta(extracted);
    outputGuid = newGuid.GetGuidString();
    const uint64_t batch = _Pipeline->BeginImportBatch(2);
    ImportAsset(QueryAsset(newGuid), true);
    ImportAsset(sourceMeta, true);
    _Pipeline->EndImportBatch(batch);
    ImportDependents(relativeMatPath);
    return true;
}

#pragma once
#include "IAssetsPipeline.h"
#include "ITaskApi.h"
#include "../Shared/AssetArtifactIO.h"
#include <QDateTime>
#include <map>

class IShaderCompiler;
struct AssetImportRequest {
    IShaderCompiler * Compiler = nullptr; // Initialized editor module; kept alive until all tasks finish.
    std::string GuidString;
    std::string AssetsRoot;
    std::string LibraryRoot;
    std::string SourcePath;
    std::string MetaJson;
    std::map<std::string, std::string> SourcePaths;
    eAssetImporterType Importer = eAssetImporterType::Unknown;
    uint64_t Revision = 0;
    bool HasPrevious = false;
    bool Force = false;
    AssetImportManifest Previous;
};

struct AssetImportJob {
    explicit AssetImportJob(const AssetImportRequest & request) : Request(request) {}
    ~AssetImportJob();
    AssetImportRequest Request;
    AssetImportResult Result;
    Json::Value Settings;
    Json::Value Identities{Json::arrayValue};
    QByteArray SourceBytes;
    std::string Stage;
    qint64 SourceSize = 0;
    QDateTime SourceModified;
    std::map<std::string, uint64_t> IDs;
    std::set<uint64_t> UsedIDs;
    std::set<std::string> ProducedKeys;
    std::map<std::string, std::pair<qint64, QDateTime>> DependencyStamps;
    Foundation::ITaskContext * Context = nullptr;

    bool Cancelled();
    bool Fail(const std::string & error);
    uint64_t ID(eAssetType type, const std::string & key, const std::string & name);
    bool Emit(eAssetType type, const std::string & key, const std::string & name,
        Json::Value metadata, const QByteArray & payload = {});
    bool AddDependency(const std::string & relativePath, QByteArray * bytes = nullptr);
};

bool RunAssetImport(AssetImportJob & job, Foundation::ITaskContext & context);
bool ImportTexture(AssetImportJob & job);
bool ImportTextureBytes(AssetImportJob & job, QByteArray bytes, const std::string & key,
    const std::string & name, const Json::Value & settings);
bool ImportModel(AssetImportJob & job);
bool ImportMedia(AssetImportJob & job);
bool ImportFont(AssetImportJob & job);
bool ImportDocument(AssetImportJob & job);
bool ImportShader(AssetImportJob & job);

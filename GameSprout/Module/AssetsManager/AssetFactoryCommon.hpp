#pragma once

#include "Header.h"
#include "ImportSettingsJson.hpp"
#include <sstream>

// 没有专用导入参数的类型仍保存 ImportSettings: {}。
template<typename Interface>
class EmptyAssetImportSettings : public Interface, public IImportSettingConverter {
public:
    bool ConvertToJson(Json::Value & value) const override {
        Json::Value result(Json::objectValue);
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        return value.isObject();
    }
};

// 材质、动画、场景等属性位于源文件，Meta 只保存通用导入配置。
template<typename Interface>
class NativeAssetImportSettings : public Interface, public IImportSettingConverter {
public:
    uint64_t GetMainObjectID() const override { return _MainObjectID; }
    void SetMainObjectID(const uint64_t value) override { _MainObjectID = value; }

    bool ConvertToJson(Json::Value & value) const override {
        Json::Value result(Json::objectValue);
        result["MainObjectID"] = static_cast<Json::UInt64>(_MainObjectID);
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        if (!value.isObject()) return false;
        uint64_t mainObjectID = 0;
        if (!AssetSettingsJson::Read(value, "MainObjectID", mainObjectID)) return false;
        _MainObjectID = mainObjectID;
        return true;
    }

private:
    uint64_t _MainObjectID = 0;
};

// 每种 Meta 内部创建、还原并释放自己的具体设置对象。
template<typename Settings>
class AssetMetaWithSettings : public AssetMeta {
public:
    virtual ~AssetMetaWithSettings() { delete _Settings; }

    AssetMetaWithSettings(const eAssetImporterType type, const Guid & guid, const std::string & path)
        : AssetMeta(type, guid, path), _Settings(new Settings()) {}

    AssetMetaWithSettings(const AssetMetaWithSettings &) = delete;
    AssetMetaWithSettings & operator=(const AssetMetaWithSettings &) = delete;

    IAssetImportSettings * GetImportSettings() const override { return _Settings; }

    bool ApplySettings(const Json::Value & value) override {
        Settings * candidate = new Settings();
        Settings * previous = _Settings;
        try {
            if (!candidate->ConvertFromJson(value)) { delete candidate; return false; }
            _Settings = candidate;
            if (!SaveMeta()) {
                _Settings = previous;
                delete candidate;
                return false;
            }
        } catch (...) {
            _Settings = previous;
            delete candidate;
            throw;
        }
        delete previous;
        return true;
    }

    bool ConvertToJson(Json::Value & value) const override {
        const char * typeName = GetAssetImporterTypeName(_ImporterType);
        if (!_Guid.IsValid() || nullptr == typeName || nullptr == _Settings) return false;
        Json::Value result(Json::objectValue);
        if (!_Settings->ConvertToJson(result["ImportSettings"])) return false;
        result["Guid"] = _Guid.GetGuidString();
        result["ImporterType"] = typeName;
        result["Version"] = 1;
        result["SubAssets"] = _SubAssets;
        result["Path"] = GetPath();
        std::string error;
        if (!AssetMetaFormat::Validate(result, error)) return false;
        value.swap(result);
        return true;
    }

    bool ConvertFromJson(const Json::Value & value) override {
        std::string error;
        if (!AssetMetaFormat::Validate(value, error)) return false;
        const Guid guid(value["Guid"].asString());
        const char * typeName = GetAssetImporterTypeName(_ImporterType);
        if (guid != _Guid || !typeName || value["ImporterType"].asString() != typeName) return false;
        Settings * settings = new Settings();
        try {
            if (!settings->ConvertFromJson(value["ImportSettings"])) {
                delete settings;
                return false;
            }
        } catch (...) {
            delete settings;
            throw;
        }
        // 定位路径由当前扫描位置决定，不能被复制或移动前的 JSON 旧路径覆盖。
        delete _Settings;
        _Settings = settings;
        _SubAssets = value["SubAssets"];
        return true;
    }

private:
    Settings * _Settings;
};

inline bool ResolveAssetEventPath(const Foundation::FileSystemWatcherEvent & event,
                                 QString & sourcePath, std::string & relativePath) {
    if (nullptr == g_IProject) return false;
    const QString projectRoot = QString::fromStdString(g_IProject->GetProjectRoot());
    const QString eventPath = QString::fromStdString(event.Path);
    if (projectRoot.isEmpty() || !QDir::isAbsolutePath(projectRoot) || !QDir::isAbsolutePath(eventPath)) return false;

    const QFileInfo sourceInfo(eventPath);
    if (!sourceInfo.exists()) return false; // 创建事件派发时，源文件可能已经被删除。
    const bool directory = event.Type == Foundation::eWatcherType::Directory;
    if (sourceInfo.isDir() != directory || (!directory && !sourceInfo.isFile())) return false;
    const QString assetsRoot = QDir(QDir(projectRoot).filePath(QStringLiteral("Assets"))).canonicalPath();
    const QString sourceDirectory = sourceInfo.dir().canonicalPath();
    if (assetsRoot.isEmpty() || sourceDirectory.isEmpty() || sourceInfo.fileName().isEmpty()) return false;
    sourcePath = QDir(sourceDirectory).filePath(sourceInfo.fileName());
    const QString relative = QDir(assetsRoot).relativeFilePath(sourcePath);
    if (!QDir::isRelativePath(relative) || relative == QStringLiteral(".") || relative == QStringLiteral("..")
        || relative.startsWith(QStringLiteral("../"))) return false;
    relativePath = relative.toStdString();
    return true;
}

template<typename Meta>
Meta * LoadAssetMetaFile(const QString & metaPath, const std::string & relativePath) {
    QFile file(metaPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot read asset meta:" << metaPath << file.errorString();
        return nullptr;
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) return nullptr;
    Json::CharReaderBuilder reader;
    reader["failIfExtra"] = true;
    reader["rejectDupKeys"] = true;
    std::istringstream input(data.toStdString());
    Json::Value value;
    std::string errors;
    if (!Json::parseFromStream(reader, input, &value, &errors) || !value.isObject()
        || !value.isMember("Guid") || !value["Guid"].isString()) {
        qWarning() << "Invalid asset meta JSON:" << metaPath << QString::fromStdString(errors);
        return nullptr;
    }
    const Guid guid(value["Guid"].asString());
    if (!guid.IsValid()) {
        qWarning() << "Invalid asset GUID:" << metaPath;
        return nullptr;
    }
    Meta * meta = new Meta(guid, relativePath);
    try {
        if (!meta->ConvertFromJson(value)) {
            delete meta;
            qWarning() << "Invalid asset meta settings or type:" << metaPath;
            return nullptr;
        }
    } catch (...) {
        delete meta;
        throw;
    }
    return meta;
}

template<typename Meta>
Meta * CreateAssetMeta(const Foundation::FileSystemWatcherEvent & event) {
    QString sourcePath;
    std::string relativePath;
    if (!ResolveAssetEventPath(event, sourcePath, relativePath)) {
        qWarning() << "Cannot resolve source inside project Assets:" << QString::fromStdString(event.Path);
        return nullptr;
    }
    const QString metaPath = sourcePath + QStringLiteral(".") + QString::fromUtf8(META_EXTENSION);
    if (QFileInfo::exists(metaPath)) return LoadAssetMetaFile<Meta>(metaPath, relativePath);
    if (nullptr == g_IGuidCreator) return nullptr;

    QFile file(metaPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        // 另一创建者可能已经写入 Meta；只读取，不覆盖、不重新分配 GUID。
        if (QFileInfo::exists(metaPath)) return LoadAssetMetaFile<Meta>(metaPath, relativePath);
        qWarning() << "Cannot create asset meta:" << metaPath << file.errorString();
        return nullptr;
    }
    file.close();
    Meta * meta = nullptr;
    try {
        const Guid guid = g_IGuidCreator->CreateGuid();
        if (!guid.IsValid()) {
            QFile::remove(metaPath);
            return nullptr;
        }
        meta = new Meta(guid, relativePath);
        if (!meta->TrySaveToMetaFile()) {
            delete meta;
            QFile::remove(metaPath);
            return nullptr;
        }
    } catch (...) {
        delete meta;
        QFile::remove(metaPath);
        throw;
    }
    return meta;
}

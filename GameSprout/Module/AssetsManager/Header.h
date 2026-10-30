#pragma once

#include <QDir>
#include <QString>
#include <QFileSystemWatcher>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <unordered_map>
#include <sstream>

#include <JsonCpp/json.hpp>

#include "IAssetsManager.h"
#include "IProject.h"
#include "IConsole.h"
#include "IGuidCreator.h"
#include "IIDEWindow.h"

#include "SafeSystem.hpp"
#include "SafeString.hpp"
UsingGameSprout;

inline extern GameSproutModuleApi * g_GameSproutModuleApi = nullptr;
inline extern IProject * g_IProject = nullptr;
inline extern IConsole * g_IConsole = nullptr;
inline extern IIDEWindow * g_IIDEWindow = nullptr;
inline extern IGuidCreator * g_IGuidCreator = nullptr;

#define META_EXTENSION "meta"

static std::string ToLower(const std::string & str) {
    std::string result = str;

    for (char & character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }

    return result;
}

// 后缀只用于识别源资源类别，不代表相应解码器或运行时已经实现。
// .asset 无法仅靠后缀区分具体内容，统一识别为 DataAsset。
inline eAssetImporterType GetAssetImporterType(const std::string & extension) {
    struct ExtensionType { const char * Extension; eAssetImporterType Type; };
    static constexpr ExtensionType extensions[] = {
        {"png", eAssetImporterType::Texture},
        {"jpg", eAssetImporterType::Texture},
        {"jpeg", eAssetImporterType::Texture},
        {"tga", eAssetImporterType::Texture},
        {"bmp", eAssetImporterType::Texture},
        {"tif", eAssetImporterType::Texture},
        {"tiff", eAssetImporterType::Texture},
        {"psd", eAssetImporterType::Texture},
        {"exr", eAssetImporterType::Texture},
        {"hdr", eAssetImporterType::Texture},
        {"dds", eAssetImporterType::Texture},
        {"ktx", eAssetImporterType::Texture},
        {"ktx2", eAssetImporterType::Texture},
        {"wav", eAssetImporterType::Audio},
        {"ogg", eAssetImporterType::Audio},
        {"mp3", eAssetImporterType::Audio},
        {"flac", eAssetImporterType::Audio},
        {"aif", eAssetImporterType::Audio},
        {"aiff", eAssetImporterType::Audio},
        {"fbx", eAssetImporterType::Model},
        {"obj", eAssetImporterType::Model},
        {"dae", eAssetImporterType::Model},
        {"dxf", eAssetImporterType::Model},
        {"blend", eAssetImporterType::Model},
        {"gltf", eAssetImporterType::Model},
        {"glb", eAssetImporterType::Model},
        {"mesh", eAssetImporterType::Mesh},
        {"mat", eAssetImporterType::Material},
        {"shader", eAssetImporterType::Shader},
        {"hlsli", eAssetImporterType::ShaderInclude},
        {"ttf", eAssetImporterType::Font},
        {"otf", eAssetImporterType::Font},
        {"fontsettings", eAssetImporterType::Font},
        {"mp4", eAssetImporterType::Video},
        {"mov", eAssetImporterType::Video},
        {"webm", eAssetImporterType::Video},
        {"avi", eAssetImporterType::Video},
        {"m4v", eAssetImporterType::Video},
        {"ogv", eAssetImporterType::Video},
        {"txt", eAssetImporterType::Text},
        {"json", eAssetImporterType::Text},
        {"xml", eAssetImporterType::Text},
        {"csv", eAssetImporterType::Text},
        {"tsv", eAssetImporterType::Text},
        {"yaml", eAssetImporterType::Text},
        {"yml", eAssetImporterType::Text},
        {"md", eAssetImporterType::Text},
        {"html", eAssetImporterType::Text},
        {"htm", eAssetImporterType::Text},
        {"fnt", eAssetImporterType::Text},
        {"manifest", eAssetImporterType::Text},
        {"bytes", eAssetImporterType::Text},
        {"cs", eAssetImporterType::Script},
        {"lua", eAssetImporterType::Script},
        {"js", eAssetImporterType::Script},
        {"ts", eAssetImporterType::Script},
        {"scene", eAssetImporterType::Scene},
        {"unity", eAssetImporterType::Scene},
        {"anim", eAssetImporterType::AnimationClip},
        {"controller", eAssetImporterType::AnimatorController},
        {"overridecontroller", eAssetImporterType::AnimatorOverrideController},
        {"mask", eAssetImporterType::AvatarMask},
        {"mixer", eAssetImporterType::AudioMixer},
        {"physicmaterial", eAssetImporterType::PhysicsMaterial},
        {"physicsmaterial2d", eAssetImporterType::PhysicsMaterial2D},
        {"rendertexture", eAssetImporterType::RenderTexture},
        {"cubemap", eAssetImporterType::Cubemap},
        {"spriteatlas", eAssetImporterType::SpriteAtlas},
        // 本项目的显式地形数据扩展名；Unity 通常将 TerrainData 保存在 .asset。
        {"terraindata", eAssetImporterType::TerrainData},
        {"terrainlayer", eAssetImporterType::TerrainLayer},
        {"shadervariants", eAssetImporterType::ShaderVariantCollection},
        {"asset", eAssetImporterType::DataAsset},
        {"bin", eAssetImporterType::DataAsset},
        {"lighting", eAssetImporterType::LightingSettings},
        {"giparams", eAssetImporterType::LightmapParameters},
    };
    const std::string ext = ToLower(extension);
    for (const ExtensionType & entry : extensions) {
        if (ext == entry.Extension) return entry.Type;
    }
    return eAssetImporterType::Unknown;
}

inline const char * GetAssetImporterTypeName(const eAssetImporterType type) {
    switch (type) {
    case eAssetImporterType::Texture: return "Texture";
    case eAssetImporterType::Audio: return "Audio";
    case eAssetImporterType::Mesh: return "Mesh";
    case eAssetImporterType::Model: return "Model";
    case eAssetImporterType::Material: return "Material";
    case eAssetImporterType::Shader: return "Shader";
    case eAssetImporterType::ComputeShader: return "ComputeShader";
    case eAssetImporterType::ShaderInclude: return "ShaderInclude";
    case eAssetImporterType::Font: return "Font";
    case eAssetImporterType::Video: return "Video";
    case eAssetImporterType::Text: return "Text";
    case eAssetImporterType::Script: return "Script";
    case eAssetImporterType::Scene: return "Scene";
    case eAssetImporterType::AnimationClip: return "AnimationClip";
    case eAssetImporterType::AnimatorController: return "AnimatorController";
    case eAssetImporterType::AnimatorOverrideController: return "AnimatorOverrideController";
    case eAssetImporterType::AvatarMask: return "AvatarMask";
    case eAssetImporterType::AudioMixer: return "AudioMixer";
    case eAssetImporterType::PhysicsMaterial: return "PhysicsMaterial";
    case eAssetImporterType::PhysicsMaterial2D: return "PhysicsMaterial2D";
    case eAssetImporterType::RenderTexture: return "RenderTexture";
    case eAssetImporterType::Cubemap: return "Cubemap";
    case eAssetImporterType::SpriteAtlas: return "SpriteAtlas";
    case eAssetImporterType::TerrainData: return "TerrainData";
    case eAssetImporterType::TerrainLayer: return "TerrainLayer";
    case eAssetImporterType::ShaderVariantCollection: return "ShaderVariantCollection";
    case eAssetImporterType::DataAsset: return "DataAsset";
    case eAssetImporterType::LightingSettings: return "LightingSettings";
    case eAssetImporterType::LightmapParameters: return "LightmapParameters";
    case eAssetImporterType::Folder: return "Folder";
    default: return nullptr;
    }
}

class IImportSettingConverter {
public:
    virtual ~IImportSettingConverter() {}

    virtual bool ConvertToJson(Json::Value & value) const = 0;
    virtual bool ConvertFromJson(const Json::Value & value) = 0;
};

class AssetMeta : public IAssetMeta {
public:
    virtual ~AssetMeta() {}
    // path 相对于项目的 Assets 目录，例如 Textures/Bear.png。
    AssetMeta(const eAssetImporterType type, const Guid guid, const std::string & path) : IAssetMeta(type, guid), _Path(path) {}

    const std::string & GetPath() const override {return _Path;}
    void SetPath(const std::string & path) { _Path = path; }
    std::vector<IAsset *> GetSubAssets() const override;
    eAssetImportState GetImportState() const override;
    std::string GetImportError() const override;


    virtual bool ConvertToJson(Json::Value & value) const = 0;
    virtual bool ConvertFromJson(const Json::Value & value) = 0;

    bool SerializeMeta(std::string & json) const override {
        Json::Value value;
        if (!ConvertToJson(value)) return false;
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        json = Json::writeString(writer, value);
        return true;
    }

    bool SetSubAssets(const std::string & json) override {
        Json::CharReaderBuilder reader;
        std::istringstream input(json);
        Json::Value value;
        std::string error;
        if (!Json::parseFromStream(reader, input, &value, &error) || !value.isArray()) return false;
        _SubAssets = value;
        return true;
    }

    bool SaveMeta() override { return TrySaveToMetaFile(); }
    virtual bool ApplySettings(const Json::Value & value) = 0;

    virtual void SaveToMetaFile() {
        TrySaveToMetaFile();
    }

    // 创建流程需要获知失败，避免遗留空 Meta 或返回未保存的对象。
    bool TrySaveToMetaFile() {
        const QString path = QDir::fromNativeSeparators(QString::fromStdString(_Path));
        const QString relativePath = QDir::cleanPath(path);
        if (path.isEmpty() || !QDir::isRelativePath(path) || relativePath == QStringLiteral(".")
            || relativePath == QStringLiteral("..") || relativePath.startsWith(QStringLiteral("../"))) {
            qWarning() << "Cannot save asset meta: path must be relative to the Assets directory:" << path;
            return false;
        }

        if (nullptr == g_IProject) {
            qWarning() << "Cannot save asset meta: project is unavailable.";
            return false;
        }

        const QString projectRoot = QString::fromStdString(g_IProject->GetProjectRoot());
        if (projectRoot.isEmpty() || !QDir::isAbsolutePath(projectRoot)) {
            qWarning() << "Cannot save asset meta: project root must be absolute:" << projectRoot;
            return false;
        }

        const QString assetsRoot = QDir(projectRoot).filePath(QStringLiteral("Assets"));
        const QString assetPath = QDir::cleanPath(QDir(assetsRoot).filePath(relativePath));

        // _Path 不包含 Assets/ 前缀或 .meta 后缀。
        const QString metaPath = assetPath + QStringLiteral(".") + QString::fromUtf8(META_EXTENSION);

        Json::Value value;
        if (!ConvertToJson(value)) {
            qWarning() << "Cannot save asset meta: JSON conversion failed:" << metaPath;
            return false;
        }

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "    ";
        writer["emitUTF8"] = true;
        const std::string content = Json::writeString(writer, value) + "\n";

        QSaveFile file(metaPath);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) {
            qWarning() << "Cannot open asset meta for saving:" << metaPath << file.errorString();
            return false;
        }

        const qint64 size = static_cast<qint64>(content.size());
        if (file.write(content.data(), size) != size) {
            qWarning() << "Cannot write complete asset meta:" << metaPath << file.errorString();
            file.cancelWriting();
            return false;
        }

        if (!file.commit()) {
            qWarning() << "Cannot commit asset meta:" << metaPath << file.errorString();
            return false;
        }
        return true;
    }

protected:
    Json::Value _SubAssets{Json::arrayValue};

private:
    std::string _Path;
};


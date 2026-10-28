#pragma once

#include <QDir>
#include <QString>
#include <QFileSystemWatcher>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <unordered_map>

#include <JsonCpp/json.hpp>

#include "IAssetsManager.h"
#include "IProject.h"
#include "IConsole.h"
#include "IGuidCreator.h"
#include "IIDEWindow.h"

#include "SafeSystem.hpp"
#include "SafeString.hpp"
UsingGameSprout;

inline extern GameSproutApi * g_GameSproutApi = nullptr;
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
inline eAssetType GetAssetType(const std::string & extension) {
    struct ExtensionType { const char * Extension; eAssetType Type; };
    static constexpr ExtensionType extensions[] = {
        {"png", eAssetType::Texture},
        {"jpg", eAssetType::Texture},
        {"jpeg", eAssetType::Texture},
        {"tga", eAssetType::Texture},
        {"bmp", eAssetType::Texture},
        {"tif", eAssetType::Texture},
        {"tiff", eAssetType::Texture},
        {"psd", eAssetType::Texture},
        {"exr", eAssetType::Texture},
        {"hdr", eAssetType::Texture},
        {"dds", eAssetType::Texture},
        {"ktx", eAssetType::Texture},
        {"ktx2", eAssetType::Texture},
        {"wav", eAssetType::Audio},
        {"ogg", eAssetType::Audio},
        {"mp3", eAssetType::Audio},
        {"flac", eAssetType::Audio},
        {"aif", eAssetType::Audio},
        {"aiff", eAssetType::Audio},
        {"fbx", eAssetType::Model},
        {"obj", eAssetType::Model},
        {"dae", eAssetType::Model},
        {"dxf", eAssetType::Model},
        {"blend", eAssetType::Model},
        {"gltf", eAssetType::Model},
        {"glb", eAssetType::Model},
        {"mesh", eAssetType::Mesh},
        {"mat", eAssetType::Material},
        {"shader", eAssetType::Shader},
        {"vert", eAssetType::Shader},
        {"frag", eAssetType::Shader},
        {"glsl", eAssetType::Shader},
        {"metal", eAssetType::Shader},
        {"spv", eAssetType::Shader},
        {"compute", eAssetType::ComputeShader},
        {"comp", eAssetType::ComputeShader},
        {"hlsl", eAssetType::ShaderInclude},
        {"hlsli", eAssetType::ShaderInclude},
        {"cginc", eAssetType::ShaderInclude},
        {"glslinc", eAssetType::ShaderInclude},
        {"cg", eAssetType::ShaderInclude},
        {"ttf", eAssetType::Font},
        {"otf", eAssetType::Font},
        {"fontsettings", eAssetType::Font},
        {"mp4", eAssetType::Video},
        {"mov", eAssetType::Video},
        {"webm", eAssetType::Video},
        {"avi", eAssetType::Video},
        {"m4v", eAssetType::Video},
        {"ogv", eAssetType::Video},
        {"txt", eAssetType::Text},
        {"json", eAssetType::Text},
        {"xml", eAssetType::Text},
        {"csv", eAssetType::Text},
        {"tsv", eAssetType::Text},
        {"yaml", eAssetType::Text},
        {"yml", eAssetType::Text},
        {"md", eAssetType::Text},
        {"html", eAssetType::Text},
        {"htm", eAssetType::Text},
        {"fnt", eAssetType::Text},
        {"manifest", eAssetType::Text},
        {"bytes", eAssetType::Text},
        {"cs", eAssetType::Script},
        {"lua", eAssetType::Script},
        {"js", eAssetType::Script},
        {"ts", eAssetType::Script},
        {"scene", eAssetType::Scene},
        {"unity", eAssetType::Scene},
        {"anim", eAssetType::AnimationClip},
        {"controller", eAssetType::AnimatorController},
        {"overridecontroller", eAssetType::AnimatorOverrideController},
        {"mask", eAssetType::AvatarMask},
        {"mixer", eAssetType::AudioMixer},
        {"physicmaterial", eAssetType::PhysicsMaterial},
        {"physicsmaterial2d", eAssetType::PhysicsMaterial2D},
        {"rendertexture", eAssetType::RenderTexture},
        {"cubemap", eAssetType::Cubemap},
        {"spriteatlas", eAssetType::SpriteAtlas},
        // 本项目的显式地形数据扩展名；Unity 通常将 TerrainData 保存在 .asset。
        {"terraindata", eAssetType::TerrainData},
        {"terrainlayer", eAssetType::TerrainLayer},
        {"shadervariants", eAssetType::ShaderVariantCollection},
        {"asset", eAssetType::DataAsset},
        {"bin", eAssetType::DataAsset},
        {"lighting", eAssetType::LightingSettings},
        {"giparams", eAssetType::LightmapParameters},
    };
    const std::string ext = ToLower(extension);
    for (const ExtensionType & entry : extensions) {
        if (ext == entry.Extension) return entry.Type;
    }
    return eAssetType::Unknown;
}

inline const char * GetAssetTypeName(const eAssetType type) {
    switch (type) {
    case eAssetType::Texture: return "Texture";
    case eAssetType::Audio: return "Audio";
    case eAssetType::Mesh: return "Mesh";
    case eAssetType::Model: return "Model";
    case eAssetType::Material: return "Material";
    case eAssetType::Shader: return "Shader";
    case eAssetType::ComputeShader: return "ComputeShader";
    case eAssetType::ShaderInclude: return "ShaderInclude";
    case eAssetType::Font: return "Font";
    case eAssetType::Video: return "Video";
    case eAssetType::Text: return "Text";
    case eAssetType::Script: return "Script";
    case eAssetType::Scene: return "Scene";
    case eAssetType::AnimationClip: return "AnimationClip";
    case eAssetType::AnimatorController: return "AnimatorController";
    case eAssetType::AnimatorOverrideController: return "AnimatorOverrideController";
    case eAssetType::AvatarMask: return "AvatarMask";
    case eAssetType::AudioMixer: return "AudioMixer";
    case eAssetType::PhysicsMaterial: return "PhysicsMaterial";
    case eAssetType::PhysicsMaterial2D: return "PhysicsMaterial2D";
    case eAssetType::RenderTexture: return "RenderTexture";
    case eAssetType::Cubemap: return "Cubemap";
    case eAssetType::SpriteAtlas: return "SpriteAtlas";
    case eAssetType::TerrainData: return "TerrainData";
    case eAssetType::TerrainLayer: return "TerrainLayer";
    case eAssetType::ShaderVariantCollection: return "ShaderVariantCollection";
    case eAssetType::DataAsset: return "DataAsset";
    case eAssetType::LightingSettings: return "LightingSettings";
    case eAssetType::LightmapParameters: return "LightmapParameters";
    case eAssetType::Folder: return "Folder";
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
    AssetMeta(const eAssetType type, const Guid guid, const std::string & path) : IAssetMeta(type, guid), _Path(path) {}

    const std::string & GetPath() const override {return _Path;}
    void SetPath(const std::string & path) { _Path = path; }


    virtual bool ConvertToJson(Json::Value & value) const = 0;
    virtual bool ConvertFromJson(const Json::Value & value) = 0;

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

private:
    std::string _Path;
};


#pragma once

#include "AssetArtifactIO.h"
#include <set>

// The current Meta envelope is fixed. ImportSettings members may still use their
// documented defaults; missing envelope fields are not an older format to upgrade.
namespace AssetMetaFormat {
inline const char * ImporterTypeName(const eAssetImporterType type) {
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

inline bool ValidateSubAssets(const Json::Value & value, std::string & error) {
    error.clear();
    const auto fail = [&error](const char * message) { error = message; return false; };
    if (!value.isArray()) return fail("Meta requires a SubAssets array");
    std::set<uint64_t> localIDs;
    std::set<std::string> sourceKeys;
    for (const Json::Value & entry : value) {
        if (!entry.isObject() || !entry["LocalID"].isUInt64() || !entry["LocalID"].asUInt64()
            || !entry["Type"].isUInt() || !entry["Type"].asUInt() || entry["Type"].asUInt() > UINT16_MAX
            || !entry["SourceKey"].isString() || entry["SourceKey"].asString().empty()
            || !entry["Name"].isString()
            || !localIDs.insert(entry["LocalID"].asUInt64()).second
            || !sourceKeys.insert(std::to_string(entry["Type"].asUInt())+":"+entry["SourceKey"].asString()).second)
            return fail("Meta has an invalid or duplicate subasset identity");
    }
    return true;
}

inline bool Validate(const Json::Value & value, std::string & error) {
    error.clear();
    const auto fail = [&error](const char * message) { error = message; return false; };
    if (!value.isObject()) return fail("Meta must be an object");
    if (!value["Version"].isUInt() || value["Version"].asUInt() != 1)
        return fail("Meta requires Version 1");
    if (value.isMember("AssetType")) return fail("Meta uses the removed AssetType field");
    if (!value["Guid"].isString() || !Guid(value["Guid"].asString()).IsValid())
        return fail("Meta requires a valid Guid");
    if (!value["ImporterType"].isString()) return fail("Meta requires ImporterType");
    bool importerKnown = false;
    for (uint16_t type = 1; type <= 254; ++type) {
        const char * name = ImporterTypeName(static_cast<eAssetImporterType>(type));
        if (name && value["ImporterType"].asString() == name) { importerKnown = true; break; }
    }
    if (!importerKnown) return fail("Meta has an unknown ImporterType");
    if (!value["Path"].isString() || !AssetArtifactIO::RelativePath(value["Path"].asString()))
        return fail("Meta requires a valid path relative to Assets");
    if (!value["ImportSettings"].isObject()) return fail("Meta requires an ImportSettings object");
    return ValidateSubAssets(value["SubAssets"], error);
}
}

#pragma once
#include "Define.h"
#include <QString>

inline QString AssetDisplayName(eAssetType type) {
    switch (type) {
#define INSPECTOR_ASSET_NAME(Name) case eAssetType::Name: return QStringLiteral(#Name);
    INSPECTOR_ASSET_NAME(Texture)
    INSPECTOR_ASSET_NAME(Mesh)
    INSPECTOR_ASSET_NAME(Model)
    INSPECTOR_ASSET_NAME(Material)
    INSPECTOR_ASSET_NAME(Shader)
    INSPECTOR_ASSET_NAME(ComputeShader)
    INSPECTOR_ASSET_NAME(Font)
    INSPECTOR_ASSET_NAME(RenderTexture)
    INSPECTOR_ASSET_NAME(Cubemap)
    INSPECTOR_ASSET_NAME(SpriteAtlas)
    INSPECTOR_ASSET_NAME(Sprite)
    INSPECTOR_ASSET_NAME(Skeleton)
    INSPECTOR_ASSET_NAME(Avatar)
    INSPECTOR_ASSET_NAME(AnimationClip)
    INSPECTOR_ASSET_NAME(AnimatorController)
    INSPECTOR_ASSET_NAME(AnimatorOverrideController)
    INSPECTOR_ASSET_NAME(AvatarMask)
    INSPECTOR_ASSET_NAME(Audio)
    INSPECTOR_ASSET_NAME(AudioMixer)
    INSPECTOR_ASSET_NAME(Video)
    INSPECTOR_ASSET_NAME(Text)
    INSPECTOR_ASSET_NAME(Script)
    INSPECTOR_ASSET_NAME(DataAsset)
    INSPECTOR_ASSET_NAME(Scene)
    INSPECTOR_ASSET_NAME(Prefab)
    INSPECTOR_ASSET_NAME(PhysicsMaterial)
    INSPECTOR_ASSET_NAME(PhysicsMaterial2D)
    INSPECTOR_ASSET_NAME(TerrainData)
    INSPECTOR_ASSET_NAME(TerrainLayer)
    INSPECTOR_ASSET_NAME(ShaderVariantCollection)
    INSPECTOR_ASSET_NAME(LightingSettings)
    INSPECTOR_ASSET_NAME(LightmapParameters)
    INSPECTOR_ASSET_NAME(ShaderInclude)
    INSPECTOR_ASSET_NAME(Folder)
    INSPECTOR_ASSET_NAME(Unknown)
#undef INSPECTOR_ASSET_NAME
    }
    return QStringLiteral("Unknown");
}

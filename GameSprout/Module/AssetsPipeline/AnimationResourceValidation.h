#pragma once
#include "AssetImportJob.h"
#include "RuntimeAnimationData.h"
bool ValidateAnimationReference(AssetImportJob&,const GameSproutRuntime::ResourceReference&,eAssetType);
bool ReadAnimationPrepared(AssetImportJob&,const GameSproutRuntime::ResourceReference&,eAssetType,Json::Value&,QByteArray&);
bool ValidateMaskSkeleton(AssetImportJob&,const GameSproutRuntime::AvatarMaskData&,GameSproutRuntime::SkeletonData&);

bool AnimationCachedInputsCurrent(AssetImportJob&);

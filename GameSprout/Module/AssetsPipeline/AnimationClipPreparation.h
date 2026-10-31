#pragma once
#include "AssetImportJob.h"
#include "../Interface/AnimationImportSettings.h"

// Finalize the current raw sample format. Original poses remain untouched;
// Runtime strips the extracted trajectory only when the Animator enables it.
bool PrepareAnimationClip(AssetImportJob& job,Json::Value& descriptor,QByteArray& payload,
    const AnimationClipImportOptions& options,const std::vector<std::int32_t>& parents);

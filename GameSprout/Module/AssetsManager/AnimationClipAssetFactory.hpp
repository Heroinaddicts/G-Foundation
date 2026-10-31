#pragma once

#include "AssetFactoryCommon.hpp"
#include "../Shared/AnimationImportSettingsJson.h"
#include "../Shared/AnimationDefinition.h"

class AnimationClipImportSettings : public NativeAssetImportSettings<IAnimationClipImportSettings> {
public:
    virtual ~AnimationClipImportSettings() {}
    const GameSproutRuntime::ResourceReference& GetSourceAvatar() const override {return _SourceAvatar;}
    bool SetSourceAvatar(const GameSproutRuntime::ResourceReference& reference) override {
        if(reference.Guid.empty()&&reference.LocalID==0){_SourceAvatar={};return true;}
        if(!Guid(reference.Guid).IsValid())return false;
        GameSproutRuntime::ResourceReference checked;
        if(!AnimationDefinition::Reference(AnimationDefinition::Reference(reference),checked))return false;
        _SourceAvatar=reference;return true;
    }
    const AnimationClipImportOptions& GetClipOptions() const override {return _Options;}
    bool SetClipOptions(const AnimationClipImportOptions& value) override {
        if(!AnimationImportSettingsJson::Validate(value,false))return false;_Options=value;return true;
    }
    bool ConvertToJson(Json::Value& value) const override {
        if(!AnimationImportSettingsJson::Validate(_Options,false))return false;
        Json::Value result;NativeAssetImportSettings::ConvertToJson(result);
        if(!_SourceAvatar.Guid.empty())result["SourceAvatar"]=AnimationDefinition::Reference(_SourceAvatar);
        result["ClipOptions"]=AnimationImportSettingsJson::Write(_Options,false);value.swap(result);return true;
    }
    bool ConvertFromJson(const Json::Value& value) override {
        AnimationClipImportSettings candidate;
        if(!candidate.NativeAssetImportSettings::ConvertFromJson(value))return false;
        if(value.isMember("SourceAvatar")&&(!AnimationDefinition::Reference(value["SourceAvatar"],candidate._SourceAvatar)||!Guid(candidate._SourceAvatar.Guid).IsValid()))return false;
        if(value.isMember("ClipOptions")&&!AnimationImportSettingsJson::Read(value["ClipOptions"],candidate._Options,false))return false;
        *this=std::move(candidate);return true;
    }
private:
    AnimationClipImportOptions _Options;
    GameSproutRuntime::ResourceReference _SourceAvatar;
};

class AnimationClipMeta : public AssetMetaWithSettings<AnimationClipImportSettings> {
public:
    AnimationClipMeta(const Guid & guid, const std::string & path)
        : AssetMetaWithSettings(eAssetImporterType::AnimationClip, guid, path) {}
};

inline IAssetMeta * OnAnimationClipAssetCreate(const Foundation::FileSystemWatcherEvent & event,
    const std::string & name, const std::string & extension, void * const context) {
    return CreateAssetMeta<AnimationClipMeta>(event);
}

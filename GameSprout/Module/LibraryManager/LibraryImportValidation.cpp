#include "LibraryManager.h"
#include "../Shared/AssetArtifactIO.h"

void LibraryManager::LoadImportValidation() {
    _ImportValidations.clear();
    _ImportValidationReady = _LDBSession && _LDBSession->CreateTable("ImportValidation");
    if (!_ImportValidationReady) return;
    ILDBTable * table = _LDBSession->GetTable("ImportValidation");
    if (!table) {
        _ImportValidationReady = false;
        return;
    }
    std::map<std::string, std::string> validations;
    const bool loaded = table->ForEach([&](const std::string & key, const void * bytes, int length) {
        if (Guid(key).IsValid() && length > 0 && bytes)
            validations.emplace(key, std::string(static_cast<const char *>(bytes), static_cast<std::size_t>(length)));
        return true;
    });
    // A failed cache read only disables fast reuse. The Manifest remains the
    // authoritative published resource record and can be verified normally.
    if (loaded) _ImportValidations.swap(validations);
}

bool LibraryManager::GetImportValidation(const Guid & guid, std::string & validationJson) const {
    validationJson.clear();
    if (!_LDBSession || !guid.IsValid()) return false;
    const auto found = _ImportValidations.find(guid.GetGuidString());
    if (found == _ImportValidations.end()) return false;
    validationJson = found->second;
    return true;
}

bool LibraryManager::SaveImportValidation(const Guid & guid, const std::string & generation,
    const std::string & validationJson) {
    if (!_LDBSession || !_ImportValidationReady || !guid.IsValid() || generation.empty()) return false;
    Json::Value validation;
    if (!AssetArtifactIO::Parse(validationJson, validation) || !validation.isObject()
        || !validation["Version"].isUInt() || !validation["Generation"].isString()
        || validation["Generation"].asString() != generation) return false;
    AssetImportManifest manifest;
    if (!GetManifest(guid, manifest) || manifest.Generation != generation) return false;
    unsigned version=1;
    for(const auto& asset:manifest.Assets)if(asset.Type==eAssetType::AnimatorController||asset.Type==eAssetType::AvatarMask||(asset.Type==eAssetType::AnimationClip&&asset.SourceKey=="main"))version=2;
    if(validation["Version"].asUInt()!=version)return false;
    const std::string key = guid.GetGuidString();
    const auto previous = _ImportValidations.find(key);
    if (previous != _ImportValidations.end() && previous->second == validationJson) return true;
    const std::vector<LDBWriteOperation> operations{{"ImportValidation", key,
        std::vector<uint8_t>(validationJson.begin(), validationJson.end()), false}};
    if (!_LDBSession->WriteBatch(operations)) return false;
    _ImportValidations[key] = validationJson;
    return true;
}

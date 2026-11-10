#include "LibraryManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <set>

namespace {
std::string Key(const std::string& guid){return "guid:"+guid;}
bool ReadOwners(ILDBTable* table,const std::string& guid,std::set<std::string>& owners) {
    if(!table)return false;
    std::vector<std::uint8_t> bytes;
    if(!table->Get(Key(guid),bytes))return true;
    Json::Value value;
    if(!AssetArtifactIO::Parse(std::string(bytes.begin(),bytes.end()),value)||value["Guid"]!=guid||!value["Guids"].isArray())return false;
    for(const auto& item:value["Guids"]) {
        if(!item.isString()||!Guid(item.asString()).IsValid())return false;
        owners.insert(item.asString());
    }
    return true;
}
}

bool LibraryManager::BuildGuidReferenceIndexOperations(const AssetImportManifest* oldManifest,
    const AssetImportManifest* newManifest,std::vector<LDBWriteOperation>& operations) {
    const std::set<std::string> before=oldManifest?
        std::set<std::string>(oldManifest->ReferencedGuids.begin(),oldManifest->ReferencedGuids.end()):std::set<std::string>{};
    const std::set<std::string> after=newManifest?
        std::set<std::string>(newManifest->ReferencedGuids.begin(),newManifest->ReferencedGuids.end()):std::set<std::string>{};
    auto referenced=before;referenced.insert(after.begin(),after.end());
    const std::string owner=newManifest?newManifest->GuidString:oldManifest->GuidString;
    for(const auto& guid:referenced) {
        if(!Guid(guid).IsValid())return false;
        const bool wasReferenced=before.count(guid)!=0,isReferenced=after.count(guid)!=0;
        if(wasReferenced==isReferenced)continue;
        std::set<std::string> owners;
        if(!ReadOwners(_LDBSession->GetTable("Dependents"),guid,owners))return false;
        if(isReferenced)owners.insert(owner);else owners.erase(owner);
        if(owners.empty())operations.push_back({"Dependents",Key(guid),{},true});
        else {
            Json::Value value;value["Guid"]=guid;value["Guids"]=Json::Value(Json::arrayValue);
            for(const auto& item:owners)value["Guids"].append(item);
            const auto text=AssetArtifactIO::JsonText(value);
            operations.push_back({"Dependents",Key(guid),std::vector<std::uint8_t>(text.begin(),text.end()),false});
        }
    }
    return true;
}

std::vector<std::string> LibraryManager::FindReferenceDependents(const Guid& guid) const {
    std::set<std::string> owners;
    if(!_LDBSession||!guid.IsValid()||!ReadOwners(_LDBSession->GetTable("Dependents"),guid.GetGuidString(),owners))return {};
    return {owners.begin(),owners.end()};
}

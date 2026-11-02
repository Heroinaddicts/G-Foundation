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
    std::set<std::string> referenced;
    if(oldManifest)referenced.insert(oldManifest->ReferencedGuids.begin(),oldManifest->ReferencedGuids.end());
    if(newManifest)referenced.insert(newManifest->ReferencedGuids.begin(),newManifest->ReferencedGuids.end());
    const std::string owner=newManifest?newManifest->GuidString:oldManifest->GuidString;
    const std::set<std::string> current=newManifest?
        std::set<std::string>(newManifest->ReferencedGuids.begin(),newManifest->ReferencedGuids.end()):std::set<std::string>{};
    for(const auto& guid:referenced) {
        if(!Guid(guid).IsValid())return false;
        std::set<std::string> owners;
        if(!ReadOwners(_LDBSession->GetTable("Dependents"),guid,owners))return false;
        owners.erase(owner);
        if(current.count(guid))owners.insert(owner);
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

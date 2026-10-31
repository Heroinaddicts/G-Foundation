#include "LibraryManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QFileInfo>
#include <set>

namespace {
std::set<std::string> TextureNames(const AssetImportManifest* manifest) {
    std::set<std::string> names;
    if(manifest)for(const auto& search:manifest->TextureSearches) {
        for(const auto& path:{search.Filename,search.RelativeFilename}) {
            const QString filename=QFileInfo(QString::fromStdString(path)).fileName().toCaseFolded();
            if(!filename.isEmpty())names.insert(filename.toStdString());
        }
    }
    return names;
}
}

bool LibraryManager::BuildTextureSearchIndexOperations(const AssetImportManifest* oldManifest,
    const AssetImportManifest* newManifest,std::vector<LDBWriteOperation>& operations) {
    const auto before=TextureNames(oldManifest),after=TextureNames(newManifest);
    auto names=before;names.insert(after.begin(),after.end());
    const auto& guid=newManifest?newManifest->GuidString:oldManifest->GuidString;
    auto* table=_LDBSession->GetTable("ModelTextureDependents");
    if(!table)return names.empty();
    for(const auto& name:names) {
        const std::string key=AssetArtifactIO::Hash(QByteArray::fromStdString(name));
        std::vector<uint8_t> bytes;Json::Value value(Json::objectValue);std::set<std::string> owners;
        if(table->Get(key,bytes)) {
            if(!AssetArtifactIO::Parse(std::string(bytes.begin(),bytes.end()),value)
                ||value["Filename"]!=name||!value["Guids"].isArray())return false;
            for(const auto& owner:value["Guids"]) {if(!owner.isString())return false;owners.insert(owner.asString());}
        }
        owners.erase(guid);if(after.count(name))owners.insert(guid);
        if(owners.empty())operations.push_back({"ModelTextureDependents",key,{},true});
        else {
            value["Filename"]=name;value["Guids"]=Json::Value(Json::arrayValue);
            for(const auto& owner:owners)value["Guids"].append(owner);
            const auto text=AssetArtifactIO::JsonText(value);
            operations.push_back({"ModelTextureDependents",key,std::vector<uint8_t>(text.begin(),text.end()),false});
        }
    }
    return true;
}

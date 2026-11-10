#include "LibraryRuntimeResources.h"
#include "LibraryResourceDecode.h"
#include "ILibraryManager.h"
using LibraryResourceDecode::Parse;
LibraryRuntimeResources::LibraryRuntimeResources(IAssetsManager* assets, ILibraryManager* library)
    : _Assets(assets), _Library(library) {
    if (_Assets) _Subscribed = _Assets->RegisterAssetListener(this);
}
LibraryRuntimeResources::~LibraryRuntimeResources() {
    if (_Subscribed) _Assets->UnregisterAssetListener(this);
}

bool LibraryRuntimeResources::LoadPrefab(const GameSproutRuntime::ResourceReference& reference,
    GameSproutRuntime::PrefabData& output,std::string& error) {
    Json::Value descriptor;QByteArray bytes;
    if(!Read(reference,eAssetType::Prefab,descriptor,bytes,error))return false;
    if(descriptor["Encoding"]!="BinaryValueLE1"||descriptor["Format"]!="GameSproutPrefab") {
        error="Invalid prepared Prefab descriptor";return false;
    }
    output.Bytes.assign(reinterpret_cast<const uint8_t*>(bytes.constData()),reinterpret_cast<const uint8_t*>(bytes.constData())+bytes.size());
    return true;
}

bool LibraryRuntimeResources::Read(const GameSproutRuntime::ResourceReference& reference, eAssetType type,
    Json::Value& descriptor, QByteArray& bytes, std::string& error) {
    bytes.clear();
    QFile file;
    ResourceState* observed=nullptr;
    if(!OpenArtifact(reference,type,descriptor,file,observed,error))return false;
    const auto size=file.size();
    bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || bytes.size() != size) {
        bytes.clear();
        error="Incomplete prepared artifact";
        if(observed)observed->ReadFailed=true;
        return false;
    }
    if(observed)observed->ReadFailed=false;
    return true;
}

bool LibraryRuntimeResources::OpenArtifact(const GameSproutRuntime::ResourceReference& reference,
    eAssetType type, Json::Value& descriptor, QFile& file, ResourceState*& observed, std::string& error) {
    error.clear(); descriptor = Json::Value();
    observed=ObserveResource(reference);
    const auto fail=[&](std::string message) {
        error=std::move(message);
        if(observed)observed->ReadFailed=true;
        return false;
    };
    const Guid guid(reference.Guid);
    if (!_Library || !guid.IsValid() || reference.LocalID == 0) return fail("Invalid resource reference or unavailable LibraryManager");
    IAsset* asset = _Library->QueryAsset(guid, reference.LocalID);
    if (!asset || asset->GetAssetType() != type) return fail("Prepared resource is missing or has the wrong type: " + reference.Guid + "_" + std::to_string(reference.LocalID));
    if (asset->GetDataVersion() != AssetArtifactIO::CurrentDataVersion(type) || !Parse(asset->GetDataDescriptorJson(), descriptor)) return fail("Invalid ArtifactDB resource descriptor");
    file.setFileName(QString::fromStdString(asset->GetArtifactPath()));
    // 单次预览分配上限，错误文件不能触发无限分配。
    if (!QFileInfo(file).isAbsolute() || !file.open(QIODevice::ReadOnly)
        || file.size() < 0 || static_cast<std::uint64_t>(file.size()) != asset->GetDataSize()
        || file.size() > 512 * 1024 * 1024) return fail("Cannot read prepared artifact: " + file.fileName().toStdString());
    return true;
}

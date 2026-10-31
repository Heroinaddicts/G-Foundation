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

bool LibraryRuntimeResources::Read(const GameSproutRuntime::ResourceReference& reference, eAssetType type,
    Json::Value& descriptor, QByteArray& bytes, std::string& error) {
    error.clear(); bytes.clear(); descriptor = Json::Value();
    const Guid guid(reference.Guid);
    if (!_Library || !guid.IsValid() || reference.LocalID == 0) { error = "Invalid resource reference or unavailable LibraryManager"; return false; }
    IAsset* asset = _Library->QueryAsset(guid, reference.LocalID);
    if (!asset || asset->GetAssetType() != type) { error = "Prepared resource is missing or has the wrong type: " + reference.Guid + "_" + std::to_string(reference.LocalID); return false; }
    if (asset->GetDataVersion() != 2 || !Parse(asset->GetDataDescriptorJson(), descriptor)) { error = "Invalid ArtifactDB resource descriptor"; return false; }
    QFile file(QString::fromStdString(asset->GetArtifactPath()));
    // 单次预览分配上限，错误文件不能触发无限分配。
    if (!QFileInfo(file).isAbsolute() || !file.open(QIODevice::ReadOnly)
        || file.size() < 0 || static_cast<std::uint64_t>(file.size()) != asset->GetDataSize()
        || file.size() > 512 * 1024 * 1024) { error = "Cannot read prepared artifact: " + file.fileName().toStdString(); return false; }
    bytes = file.readAll();
    if (file.error() != QFileDevice::NoError || static_cast<std::uint64_t>(bytes.size()) != asset->GetDataSize()) {
        bytes.clear(); error = "Incomplete prepared artifact"; return false;
    }
    return true;
}

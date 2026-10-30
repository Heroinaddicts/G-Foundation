#include "AssetsManager.h"
#include <algorithm>

namespace {
eAssetImportState State(const AssetSourceRecord & record) {
    if (record.Status == "Ready") return eAssetImportState::Ready;
    if (record.Status == "Failed") return eAssetImportState::Failed;
    if (record.Status == "NotRequired") return eAssetImportState::NotRequired;
    return eAssetImportState::Pending;
}
}

std::vector<IAsset *> AssetMeta::GetSubAssets() const {
    std::vector<IAsset *> result;
    AssetsManager * owner = AssetsManager::s_Active;
    if (!owner || !owner->_Library || owner->QueryAsset(_Guid) != this) return result;
    AssetImportManifest manifest;
    if (!owner->_Library->GetManifest(_Guid, manifest)) return result;
    for (const AssetArtifact & asset : manifest.Assets) {
        if (asset.SourceKey == "main") continue;
        if (IAsset * resource = owner->_Library->QueryAsset(_Guid, asset.LocalID)) result.push_back(resource);
    }
    return result;
}

eAssetImportState AssetMeta::GetImportState() const {
    AssetsManager * owner = AssetsManager::s_Active;
    if (owner) {
        const auto found = owner->_SourceRecords.find(_Guid.GetGuidString());
        if (found != owner->_SourceRecords.end()) return State(found->second);
    }
    if (_ImporterType == eAssetImporterType::Folder || _ImporterType == eAssetImporterType::Script)
        return eAssetImportState::NotRequired;
    return eAssetImportState::Pending;
}

std::string AssetMeta::GetImportError() const {
    AssetsManager * owner = AssetsManager::s_Active;
    if (!owner) return {};
    const auto found = owner->_SourceRecords.find(_Guid.GetGuidString());
    return found == owner->_SourceRecords.end() ? std::string() : found->second.Error;
}

std::vector<IAssetMeta *> AssetsManager::GetAssetsInDirectory(const std::string & path) const {
    std::vector<IAssetMeta *> result;
    const QString directory = path.empty() ? QStringLiteral(".") : QString::fromStdString(path);
    if (directory != "." && !AssetArtifactIO::RelativePath(path)) return result;
    for (const auto & entry : _Assets) {
        if (QFileInfo(QString::fromStdString(entry.second->GetPath())).path() == directory) result.push_back(entry.second);
    }
    std::sort(result.begin(), result.end(), [](IAssetMeta * left, IAssetMeta * right) {
        const bool leftFolder = left->_ImporterType == eAssetImporterType::Folder;
        const bool rightFolder = right->_ImporterType == eAssetImporterType::Folder;
        if (leftFolder != rightFolder) return leftFolder;
        return QString::compare(QString::fromStdString(left->GetPath()), QString::fromStdString(right->GetPath()), Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool AssetsManager::RegisterAssetListener(IAssetListener * listener) {
    if (!listener) return false;
    for (const auto & entry : _AssetListeners) if (entry.second == listener) return false;
    _AssetListeners.emplace(++_NextListener, listener);
    return true;
}

void AssetsManager::UnregisterAssetListener(IAssetListener * listener) {
    for (auto it = _AssetListeners.begin(); it != _AssetListeners.end();) {
        if (it->second == listener) it = _AssetListeners.erase(it); else ++it;
    }
}

void AssetsManager::QueueAssetEvent(eAssetMetaEvent action, const AssetSourceRecord & record, const std::string & oldPath) {
    _AssetEvents.emplace_back(action, Guid(record.GuidString), record.Path, oldPath, record.Importer, State(record), record.Error);
}

void AssetsManager::DispatchAssetEvents() {
    if (_DispatchingEvents) return;
    _DispatchingEvents = true;
    struct Guard { bool & Flag; ~Guard() { Flag = false; } } guard{_DispatchingEvents};
    const size_t count = _AssetEvents.size();
    for (size_t index = 0; index < count && !_AssetEvents.empty(); ++index) {
        AssetEvent event = std::move(_AssetEvents.front());
        _AssetEvents.pop_front();
        std::vector<uint64_t> listeners;
        for (const auto & entry : _AssetListeners) listeners.push_back(entry.first);
        for (uint64_t id : listeners) {
            const auto found = _AssetListeners.find(id);
            if (found == _AssetListeners.end()) continue;
            try {
                if (event.Action == eAssetMetaEvent::Create) found->second->OnAssetCreate(event);
                else if (event.Action == eAssetMetaEvent::Modify) found->second->OnAssetModify(event);
                else found->second->OnAssetRemove(event);
            } catch (...) { qWarning("Asset event listener threw an exception"); }
        }
    }
}

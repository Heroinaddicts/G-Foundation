#include "AssetImportJob.h"
#include "IAssetsManager.h"

namespace {
std::string ExistingPath(const std::string& assetsRoot,const std::string& path) {
    if(path.empty())return {};
    const QFileInfo source(QDir(QString::fromStdString(assetsRoot)).filePath(QString::fromStdString(path)));
    // Only absence is Missing. Existing directories, unreadable files and
    // symlinks still go through AddDependency's normal validation and errors.
    return source.exists()||source.isSymLink()?path:std::string{};
}
}

std::string AssetGuidSourcePath(const AssetImportRequest& request,const std::string& guid) {
    const auto source=request.SourcePaths.find(guid);
    return source==request.SourcePaths.end()?std::string{}:ExistingPath(request.AssetsRoot,source->second);
}
std::string AssetImportJob::TrackGuidReference(const std::string& guid) {
    if(guid==Request.GuidString)return Request.SourcePath;
    auto& manifest=Result.Manifest;
    const std::string path=AssetGuidSourcePath(Request,guid);
    const auto inserted=manifest.GuidSourcePaths.insert_or_assign(guid,path).second;
    if(inserted)manifest.ReferencedGuids.push_back(guid);
    return path;
}
bool AssetGuidSourcesCurrent(const AssetImportRequest& request,const AssetImportManifest& manifest) {
    for(const auto& [guid,path]:manifest.GuidSourcePaths)if(AssetGuidSourcePath(request,guid)!=path)return false;
    return true;
}
bool AssetGuidSourcesCurrent(const AssetImportManifest& manifest,const IAssetsManager* assets,const std::string& assetsRoot) {
    if(!assets)return manifest.GuidSourcePaths.empty();
    for(const auto& [guid,path]:manifest.GuidSourcePaths) {
        const auto* source=assets->QueryAsset(Guid(guid));
        const std::string current=source?ExistingPath(assetsRoot,source->GetPath()):std::string{};
        if(current!=path)return false;
    }
    return true;
}

#include "AssetReferenceResolver.h"
#include <QFileInfo>
#include <QDir>
#include <algorithm>

namespace AssetReferenceResolver {
namespace {
void AppendCandidates(ILibraryManager* library,IAssetMeta* meta,const AssetImportManifest& manifest,
    eAssetType expected,std::vector<AssetReferenceCandidate>& result) {
    // 一份 Manifest 只读取一次，避免大型 FBX 的每个子资源重复读取/解析整份记录。
    const QDir root(QString::fromStdString(library->GetLibraryRoot()));
    for(const auto& artifact:manifest.Assets)if(artifact.Type==expected && artifact.LocalID) {
        const QString path=root.filePath(QString::fromStdString(manifest.Directory+"/"+artifact.Path));
        if(QFileInfo(path).isFile())result.push_back({{meta->_Guid.GetGuidString(),artifact.LocalID},artifact.Name,meta->GetPath(),artifact.SourceKey=="main"});
    }
}
}
std::string TypeName(eAssetType type) {
    switch(type) {
    case eAssetType::Texture:return "Texture";
    case eAssetType::Mesh:return "Mesh";
    case eAssetType::Material:return "Material";
    case eAssetType::Shader:return "Shader";
    case eAssetType::Model:return "Model";
    case eAssetType::Skeleton:return "Skeleton";
    case eAssetType::AnimationClip:return "Animation Clip";
    case eAssetType::AnimatorController:return "Animator Controller";
    case eAssetType::Avatar:return "Avatar";
    case eAssetType::AvatarMask:return "Avatar Mask";
    default:return "Unsupported";
    }
}
bool Resolve(IAssetsManager* assets,ILibraryManager* library,const GameSproutRuntime::ResourceReference& reference,
    eAssetType expected,AssetReferenceCandidate& result,std::string& error,bool requireReady) {
    error.clear();result={};
    if(!assets || !library) {error="资源服务尚未就绪。";return false;}
    if(expected!=eAssetType::Mesh && expected!=eAssetType::Texture && expected!=eAssetType::Material && expected!=eAssetType::Shader && expected!=eAssetType::Model && expected!=eAssetType::Skeleton && expected!=eAssetType::AnimationClip && expected!=eAssetType::AnimatorController && expected!=eAssetType::Avatar && expected!=eAssetType::AvatarMask) {error="此字段尚未支持该资源类型。";return false;}
    const Guid guid(reference.Guid);
    if(!guid.IsValid() || !reference.LocalID) {error="资源身份无效，需要有效 GUID 和非零 LocalID。";return false;}
    IAssetMeta* meta=assets->QueryAsset(guid);
    if(!meta || meta->_ImporterType==eAssetImporterType::Folder) {error="Missing：源资源已删除或尚未登记。";return false;}
    if(requireReady && meta->GetImportState()!=eAssetImportState::Ready) {
        error=meta->GetImportState()==eAssetImportState::Pending?"资源正在导入，请完成后重试。":"资源导入失败或未启用，请先处理导入问题。";return false;
    }
    AssetImportManifest manifest;
    if(!library->GetManifest(guid,manifest)) {error="Missing：找不到已导入的资源记录。";return false;}
    for(const auto& artifact:manifest.Assets)if(artifact.LocalID==reference.LocalID) {
        if(artifact.Type!=expected) {error="资源类型不匹配，此字段只接受 "+TypeName(expected)+"。";return false;}
        // 只借用本次查询的描述；重导入后对象地址会失效。
        IAsset* asset=library->QueryAsset(guid,reference.LocalID);
        if(!asset || asset->GetAssetType()!=expected || !QFileInfo(QString::fromStdString(asset->GetArtifactPath())).isFile()) {
            error="Missing：预处理产物不存在，请重新导入源资源。";return false;
        }
        result={{guid.GetGuidString(),reference.LocalID},artifact.Name,meta->GetPath(),artifact.SourceKey=="main"};
        return true;
    }
    error="Missing：子资源已不存在，原 GUID 和 LocalID 仍保留。";return false;
}
bool Candidates(IAssetsManager* assets,ILibraryManager* library,const ProjectAssetSelection& selection,
    eAssetType expected,std::vector<AssetReferenceCandidate>& result,std::string& error) {
    result.clear();error.clear();
    if(selection.IsSubAsset) {
        AssetReferenceCandidate item;
        if(!Resolve(assets,library,{selection.GuidString,selection.LocalID},expected,item,error))return false;
        result.push_back(std::move(item));return true;
    }
    const Guid guid(selection.GuidString);
    if(!assets || !library || !guid.IsValid() || selection.LocalID) {error="资源来源无效。";return false;}
    IAssetMeta* meta=assets->QueryAsset(guid);
    if(!meta || meta->_ImporterType==eAssetImporterType::Folder) {error="文件夹不能赋给资源字段。";return false;}
    if(meta->GetImportState()!=eAssetImportState::Ready) {error="源资源尚未成功导入，请完成导入后重试。";return false;}
    AssetImportManifest manifest;
    if(library->GetManifest(guid,manifest))AppendCandidates(library,meta,manifest,expected,result);
    if(result.empty()) {error="此源文件没有可用的 "+TypeName(expected)+" 资源。";return false;}
    return true;
}
std::vector<AssetReferenceCandidate> Enumerate(IAssetsManager* assets,ILibraryManager* library,eAssetType expected) {
    std::vector<AssetReferenceCandidate> result;
    if(!assets || !library)return result;
    for(const auto& text:library->GetManifestGuids()) {
        AssetImportManifest manifest;const Guid guid(text);
        IAssetMeta* meta=assets->QueryAsset(guid);
        if(!meta || meta->GetImportState()!=eAssetImportState::Ready || meta->_ImporterType==eAssetImporterType::Folder)continue;
        if(!library->GetManifest(guid,manifest))continue;
        AppendCandidates(library,meta,manifest,expected,result);
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) {
        if(a.SourcePath!=b.SourcePath)return a.SourcePath<b.SourcePath;
        if(a.Name!=b.Name)return a.Name<b.Name;
        return a.Reference.LocalID<b.Reference.LocalID;
    });
    return result;
}
}

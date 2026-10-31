#pragma once
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ISelectionManager.h"
#include "GameSproutRuntimeApi.h"

struct AssetReferenceCandidate {
    GameSproutRuntime::ResourceReference Reference;
    std::string Name;
    std::string SourcePath;
    bool IsMain=false;
};
namespace AssetReferenceResolver {
std::string TypeName(eAssetType type);
bool Resolve(IAssetsManager* assets,ILibraryManager* library,const GameSproutRuntime::ResourceReference& reference,
    eAssetType expected,AssetReferenceCandidate& result,std::string& error,bool requireReady=true);
bool Candidates(IAssetsManager* assets,ILibraryManager* library,const ProjectAssetSelection& selection,
    eAssetType expected,std::vector<AssetReferenceCandidate>& result,std::string& error);
std::vector<AssetReferenceCandidate> Enumerate(IAssetsManager* assets,ILibraryManager* library,eAssetType expected);
}

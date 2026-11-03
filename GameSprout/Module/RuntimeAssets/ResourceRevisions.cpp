#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"

std::map<std::uint64_t,LibraryRuntimeResources::ResourceSignature>
LibraryRuntimeResources::ReadSignatures(const Guid& guid,std::string& generation) const {
    AssetImportManifest manifest;
    std::map<std::uint64_t,ResourceSignature> signatures;
    generation.clear();
    if(!_Library||!_Library->GetManifest(guid,manifest))return signatures;
    generation=manifest.Generation;
    for(const auto& asset:manifest.Assets) {
        signatures.emplace(asset.LocalID,ResourceSignature{asset.Type,asset.DataVersion,
            asset.DataSize,asset.Hash,asset.DescriptorJson});
    }
    return signatures;
}

LibraryRuntimeResources::ObservedGuid* LibraryRuntimeResources::ObserveGuid(const std::string& text) const {
    // The common path does not access Library/LMDB or rebuild artifact signatures.
    auto found=_ObservedGuids.find(text);
    if(found!=_ObservedGuids.end())return &found->second;
    const Guid guid(text);
    if(!guid.IsValid())return nullptr;
    const std::string canonical=guid.GetGuidString();
    found=_ObservedGuids.find(canonical);
    if(found!=_ObservedGuids.end())return &found->second;
    ObservedGuid observed;
    for(auto& [id,signature]:ReadSignatures(guid,observed.PublicationGeneration)) {
        ResourceState state;state.Present=true;state.Signature=std::move(signature);
        observed.Resources.emplace(id,std::move(state));
    }
    return &_ObservedGuids.emplace(canonical,std::move(observed)).first->second;
}

LibraryRuntimeResources::ResourceState*
LibraryRuntimeResources::ObserveResource(const GameSproutRuntime::ResourceReference& reference) const {
    if(!reference.LocalID)return nullptr;
    auto* guid=ObserveGuid(reference.Guid);
    return guid?&guid->Resources.try_emplace(reference.LocalID).first->second:nullptr;
}

std::uint64_t LibraryRuntimeResources::GetResourceRevision(const GameSproutRuntime::ResourceReference& reference) const {
    const auto* state=ObserveResource(reference);
    return state?state->Revision:0;
}

void LibraryRuntimeResources::OnAssetCreate(const AssetEvent& event) {RefreshPublishedResources(event);}
void LibraryRuntimeResources::OnAssetModify(const AssetEvent& event) {RefreshPublishedResources(event);}
void LibraryRuntimeResources::OnAssetRemove(const AssetEvent& event) {RefreshPublishedResources(event);}

void LibraryRuntimeResources::RefreshPublishedResources(const AssetEvent& event) {
    const std::string guid=event.GuidValue.GetGuidString();
    const auto found=_ObservedGuids.find(guid);
    if(found==_ObservedGuids.end())return;
    std::string generation;
    auto signatures=ReadSignatures(event.GuidValue,generation);
    const bool newReadyPublication=event.State==eAssetImportState::Ready&&
        generation!=found->second.PublicationGeneration;
    std::set<ResourceKey> changed;
    for(auto& [id,state]:found->second.Resources) {
        const auto next=signatures.find(id);
        const bool present=next!=signatures.end();
        if(state.Present!=present||(present&&state.Signature!=next->second)) {
            state.Present=present;
            state.Signature=present?std::move(next->second):ResourceSignature{};
            changed.insert({guid,id});
        }
        // Rebuilding a missing/corrupt Artifact can publish identical bytes and
        // descriptors. Only a previously failed reader needs another attempt;
        // healthy resources keep their GPU/decoded allocations in this case.
        if(newReadyPublication&&state.ReadFailed) {
            state.ReadFailed=false;
            changed.insert({guid,id});
        }
        if(present)signatures.erase(next);
    }
    for(auto& [id,signature]:signatures) {
        ResourceState state;state.Present=true;state.Signature=std::move(signature);
        found->second.Resources.emplace(id,std::move(state));
        changed.insert({guid,id});
    }
    if(event.State==eAssetImportState::Ready)found->second.PublicationGeneration=std::move(generation);
    if(!changed.empty())InvalidateResources(changed);
}

void LibraryRuntimeResources::InvalidateResources(const std::set<ResourceKey>& changed) {
    std::set<ResourceKey> invalidated=changed;
    std::vector<ResourceKey> pending(changed.begin(),changed.end());
    for(std::size_t index=0;index<pending.size();++index) {
        const auto dependents=_Dependents.find(pending[index]);
        if(dependents!=_Dependents.end())for(const auto& dependent:dependents->second)
            if(invalidated.insert(dependent).second)pending.push_back(dependent);
    }
    for(const auto& key:invalidated) {
        const auto guid=_ObservedGuids.find(key.Guid);
        if(guid==_ObservedGuids.end())continue;
        const auto resource=guid->second.Resources.find(key.LocalID);
        if(resource!=guid->second.Resources.end()&&++resource->second.Revision==0)++resource->second.Revision;
    }
    if(++_Revision==0)++_Revision;
    const auto callback=ResourcesChanged;if(callback)callback();
}

void LibraryRuntimeResources::ResetDependencies(const GameSproutRuntime::ResourceReference& reference) {
    auto* state=ObserveResource(reference);
    if(!state)return;
    const ResourceKey owner{Guid(reference.Guid).GetGuidString(),reference.LocalID};
    for(const auto& dependency:state->Dependencies) {
        const auto found=_Dependents.find(dependency);
        if(found!=_Dependents.end()) {
            found->second.erase(owner);
            if(found->second.empty())_Dependents.erase(found);
        }
    }
    state->Dependencies.clear();
}

void LibraryRuntimeResources::AddDependency(const GameSproutRuntime::ResourceReference& reference,
    const GameSproutRuntime::ResourceReference& dependency) {
    auto* state=ObserveResource(reference);
    if(!state||!ObserveResource(dependency))return;
    const ResourceKey owner{Guid(reference.Guid).GetGuidString(),reference.LocalID};
    const ResourceKey target{Guid(dependency.Guid).GetGuidString(),dependency.LocalID};
    if(owner==target)return;
    if(state->Dependencies.insert(target).second)_Dependents[target].insert(owner);
}

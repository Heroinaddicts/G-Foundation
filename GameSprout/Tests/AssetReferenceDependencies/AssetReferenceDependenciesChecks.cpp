#include "LibraryManager.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QCoreApplication>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Table final:public ILDBTable {
public:
    std::map<std::string,std::vector<std::uint8_t>> Rows;
    mutable std::size_t Gets=0;
    bool IsExists(const std::string& key)const override{return Rows.count(key)!=0;}
    bool Set(const std::string& key,void* const data,int size)override {
        if(size<0||(!data&&size))return false;
        const auto* bytes=static_cast<const std::uint8_t*>(data);
        Rows[key]=size?std::vector<std::uint8_t>(bytes,bytes+size):std::vector<std::uint8_t>{};return true;
    }
    bool Get(const std::string& key,std::vector<std::uint8_t>& data)const override {
        ++Gets;
        const auto found=Rows.find(key);data.clear();if(found==Rows.end())return false;data=found->second;return true;
    }
    bool Delete(const std::string& key)override{Rows.erase(key);return true;}
    bool ForEach(const fVisitCallback& callback)const override {
        if(!callback)return false;
        for(const auto& [key,value]:Rows)if(!callback(key,value.data(),static_cast<int>(value.size())))break;
        return true;
    }
};
class Session final:public ILDBSession {
public:
    std::map<std::string,Table> Tables;
    bool Reject=false;
    bool CreateTable(const std::string& name)override{Tables.try_emplace(name);return true;}
    ILDBTable* GetTable(const std::string& name)override{const auto found=Tables.find(name);return found==Tables.end()?nullptr:&found->second;}
    bool WriteBatch(const std::vector<LDBWriteOperation>& writes)override {
        if(Reject)return false;
        for(const auto& write:writes)if(!Tables.count(write.Table))return false;
        auto next=Tables;
        for(const auto& write:writes)if(write.Delete)next[write.Table].Rows.erase(write.Key);else next[write.Table].Rows[write.Key]=write.Data;
        Tables.swap(next);return true;
    }
    void Close()override{}
};
AssetImportManifest Manifest(const std::string& guid,const std::string& path) {
    AssetImportManifest value;value.GuidString=guid;value.SourcePath=path;value.Generation="isolated-current-publication";return value;
}
void CommitIndex(LibraryManager& library,Session& session,const AssetImportManifest* previous,const AssetImportManifest& manifest) {
    std::vector<LDBWriteOperation> writes;
    Check(library.BuildIndexOperations(previous,&manifest,writes),"Cannot build reference dependency operations");
    const auto json=AssetArtifactIO::JsonText(AssetArtifactIO::ToJson(manifest));
    writes.push_back({"Manifests",manifest.GuidString,std::vector<std::uint8_t>(json.begin(),json.end()),false});
    Check(session.WriteBatch(writes),"Cannot commit reference dependency fixture");
}
void CheckIndexDeltas() {
    Session session;for(const auto* name:{"Manifests","Dependents","ModelTextureDependents"})session.CreateTable(name);
    LibraryManager library;library._LDBSession=&session;
    auto before=Manifest("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","Large.scene");
    for(unsigned index=0;index<2400;++index) {
        char suffix[9];std::snprintf(suffix,sizeof(suffix),"%08x",index);
        before.Dependencies.push_back({"Shared/Asset"+std::to_string(index)+".mesh","original-hash"});
        before.ReferencedGuids.push_back(std::string("0123456789abcdef01234567")+suffix);
        before.GuidSourcePaths.emplace(before.ReferencedGuids.back(),before.Dependencies.back().Path);
    }
    before.TextureSearches={{"Old.PNG",{}, {},"missing"},{"Shared.png",{}, {},"missing"}};
    CommitIndex(library,session,nullptr,before);
    auto shared=Manifest("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","Other.scene");
    shared.Dependencies={before.Dependencies.front()};shared.ReferencedGuids={before.ReferencedGuids.front()};
    shared.GuidSourcePaths={{shared.ReferencedGuids.front(),shared.Dependencies.front().Path}};
    shared.TextureSearches={before.TextureSearches.front()};CommitIndex(library,session,nullptr,shared);
    const auto resetReads=[&]{for(auto& [name,table]:session.Tables)table.Gets=0;};
    const auto reads=[&]{std::size_t result=0;for(const auto& [name,table]:session.Tables)result+=table.Gets;return result;};

    // A new generation, source hash or dependency content hash does not change
    // who owns an index edge. Assert IO counts rather than a timing threshold.
    auto unchanged=before;unchanged.Generation="changed-generation";unchanged.SourceHash="new-scene-hash";
    unchanged.Dependencies.front().Hash="changed-source-content";
    unchanged.TextureSearches.front().Reason="resolved";
    resetReads();std::vector<LDBWriteOperation> writes;
    Check(library.BuildIndexOperations(&before,&unchanged,writes)&&writes.empty()&&reads()==0,
        "Unchanged 2400-resource ownership performed reverse-index reads or writes");

    auto after=unchanged;
    const auto oldPath=before.Dependencies.front().Path,oldGuid=before.ReferencedGuids.front();
    after.Dependencies.front()={"Added/Resource.mesh","new-hash"};
    after.ReferencedGuids.front()="ffffffffffffffffffffffffffffffff";
    after.GuidSourcePaths.erase(oldGuid);
    after.GuidSourcePaths[after.ReferencedGuids.front()]=after.Dependencies.front().Path;
    after.TextureSearches.front().Filename="New.PNG";
    resetReads();
    Check(library.BuildIndexOperations(&before,&after,writes)&&writes.size()==6&&reads()==6,
        "One removed/added path, GUID and texture name must touch exactly six index rows");
    const auto json=AssetArtifactIO::JsonText(AssetArtifactIO::ToJson(after));
    writes.push_back({"Manifests",after.GuidString,std::vector<std::uint8_t>(json.begin(),json.end()),false});
    Check(session.WriteBatch(writes),"Cannot commit dependency differences");
    Check(library.FindDependents(oldPath)==std::vector<std::string>{shared.GuidString}&&
          library.FindReferenceDependents(Guid(oldGuid))==std::vector<std::string>{shared.GuidString}&&
          library.FindDependents("Elsewhere/oLd.PnG")==std::vector<std::string>{shared.GuidString},
        "Removing an edge erased another resource's path/GUID/texture ownership");
    Check(library.FindDependents("Added/Resource.mesh")==std::vector<std::string>{after.GuidString}&&
          library.FindReferenceDependents(Guid(after.ReferencedGuids.front()))==std::vector<std::string>{after.GuidString}&&
          library.FindDependents("Elsewhere/new.png")==std::vector<std::string>{after.GuidString},
        "Added dependency differences were not indexed");
    writes.clear();resetReads();
    Check(library.BuildIndexOperations(&after,&after,writes)&&writes.empty()&&reads()==0,
        "The committed dependency differences were unnecessarily rewritten");

    auto invalid=after;invalid.ReferencedGuids.push_back("not-a-guid");
    Check(!library.BuildIndexOperations(&invalid,&invalid,writes),"Unchanged invalid reference GUID bypassed validation");
    auto wrongOwner=after;wrongOwner.GuidString=shared.GuidString;writes.clear();
    Check(!library.BuildIndexOperations(&after,&wrongOwner,writes)&&writes.empty(),"A publication changed its owner identity");
    std::string error;Check(library.RemoveAsset(Guid(after.GuidString),error),error);
    Check(library.FindDependents("Added/Resource.mesh").empty()&&
          library.FindReferenceDependents(Guid(after.ReferencedGuids.front())).empty()&&
          library.FindDependents("Elsewhere/New.PNG").empty()&&
          library.FindDependents(oldPath)==std::vector<std::string>{shared.GuidString},
        "Owner deletion failed to remove its new edges or erased shared owners");
    library._LDBSession=nullptr;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        CheckIndexDeltas();
        Session session;for(const auto* name:{"Manifests","Dependents","ModelTextureDependents"})session.CreateTable(name);
        LibraryManager library;library._LDBSession=&session;
        const Guid target("1234567890abcdef1234567890abcdef"),owner("234567890abcdef1234567890abcdef1");
        auto resource=Manifest(target.GetGuidString(),"Old/Shape.mesh");CommitIndex(library,session,nullptr,resource);
        auto document=Manifest(owner.GetGuidString(),"Unit.prefab");
        document.ReferencedGuids={target.GetGuidString()};document.GuidSourcePaths={{target.GetGuidString(),"Old/Shape.mesh"}};
        document.Dependencies={{"Old/Shape.mesh","source-hash"}};
        CommitIndex(library,session,nullptr,document);
        Check(library.FindReferenceDependents(target)==std::vector<std::string>{owner.GetGuidString()},"Reference owner was not indexed");
        std::string error;Check(library.RemoveAsset(target,error),error);
        Check(library.FindReferenceDependents(target)==std::vector<std::string>{owner.GetGuidString()},"Deleting the target erased incoming reference identities");
        auto missing=document;missing.Dependencies.clear();missing.GuidSourcePaths[target.GetGuidString()].clear();
        CommitIndex(library,session,&document,missing);
        Check(library.FindDependents("Old/Shape.mesh").empty()&&
              library.FindReferenceDependents(target)==std::vector<std::string>{owner.GetGuidString()},
              "Reimporting Missing dropped stable GUID recovery edges or kept stale path edges");
        auto restored=missing;restored.GuidSourcePaths[target.GetGuidString()]="Restored/Shape.mesh";
        restored.Dependencies={{"Restored/Shape.mesh","restored-hash"}};CommitIndex(library,session,&missing,restored);
        Check(library.FindDependents("Restored/Shape.mesh")==std::vector<std::string>{owner.GetGuidString()}&&
              library.FindReferenceDependents(target)==std::vector<std::string>{owner.GetGuidString()},"Restored source path did not replace the dependency location");
        session.Reject=true;Check(!library.RemoveAsset(owner,error),"Rejected removal unexpectedly committed");session.Reject=false;
        Check(library.FindReferenceDependents(target)==std::vector<std::string>{owner.GetGuidString()},"Failed publication partially removed GUID edges");
        Check(library.RemoveAsset(owner,error),error);
        Check(library.FindReferenceDependents(target).empty()&&library.FindDependents("Restored/Shape.mesh").empty(),"Deleting the owner retained dangling dependency index entries");
        library._LDBSession=nullptr;
        std::cout<<"PASS dependency differences (2400 unchanged resources: zero index IO), shared owners, GUID reference indexing, target deletion, Missing reimport, path restoration, atomic failure and owner deletion\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

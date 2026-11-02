#include "LibraryManager.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
class Table final:public ILDBTable {
public:
    std::map<std::string,std::vector<std::uint8_t>> Rows;
    bool IsExists(const std::string& key)const override{return Rows.count(key)!=0;}
    bool Set(const std::string& key,void* const data,int size)override {
        if(size<0||(!data&&size))return false;
        const auto* bytes=static_cast<const std::uint8_t*>(data);
        Rows[key]=size?std::vector<std::uint8_t>(bytes,bytes+size):std::vector<std::uint8_t>{};return true;
    }
    bool Get(const std::string& key,std::vector<std::uint8_t>& data)const override {
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
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
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
        std::cout<<"PASS GUID reference indexing, target deletion, Missing reimport, path restoration, atomic failure and owner deletion\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

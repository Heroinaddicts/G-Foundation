#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "AssetArtifactIO.h"
#include "AssetValueBinary.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
void Check(bool accepted,const std::string& message) {if(!accepted)throw std::runtime_error(message);}
const std::string ModelGuid="11111111111111111111111111111111";
const std::string MaterialGuid="22222222222222222222222222222222";
const std::string OtherMaterialGuid="33333333333333333333333333333333";
const std::string ShaderGuid="44444444444444444444444444444444";
const std::string TextureGuid="55555555555555555555555555555555";
const std::string UnobservedGuid="66666666666666666666666666666666";
GameSproutRuntime::ResourceReference Reference(const std::string& guid,std::uint64_t id) {return {guid,id};}
Json::Value JsonReference(const std::string& guid,std::uint64_t id) {
    Json::Value reference(Json::objectValue);reference["Guid"]=guid;reference["LocalID"]=Json::UInt64(id);return reference;
}
class PreparedAsset final:public IAsset {
public:
    PreparedAsset(const std::string& guid,const AssetArtifact& artifact,const std::string& path)
        :ID(guid),Artifact(artifact),Path(path) {}
    const Guid ID;
    AssetArtifact Artifact;
    std::string Path;
    const Guid& GetGuid() const override{return ID;}
    std::uint64_t GetLocalID() const override{return Artifact.LocalID;}
    eAssetType GetAssetType() const override{return Artifact.Type;}
    const std::string& GetName() const override{return Artifact.Name;}
    const std::string& GetArtifactPath() const override{return Path;}
    const std::string& GetDataDescriptorJson() const override{return Artifact.DescriptorJson;}
    std::uint32_t GetDataVersion() const override{return Artifact.DataVersion;}
    std::uint64_t GetDataSize() const override{return Artifact.DataSize;}
};
class Library final:public ILibraryManager {
public:
    std::string Root;
    std::map<std::string,AssetImportManifest> Manifests;
    std::map<std::pair<std::string,std::uint64_t>,PreparedAsset> Prepared;
    mutable unsigned ManifestReads=0;
    unsigned AssetReads=0;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{throw std::runtime_error("Provider enumerated all manifests");}
    bool GetManifest(const Guid& guid,AssetImportManifest& output) const override {
        ++ManifestReads;const auto found=Manifests.find(guid.GetGuidString());
        if(found==Manifests.end())return false;output=found->second;return true;
    }
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid& guid,std::uint64_t id) override {
        ++AssetReads;const auto found=Prepared.find({guid.GetGuidString(),id});
        return found==Prepared.end()?nullptr:&found->second;
    }
    AssetArtifact& Artifact(const GameSproutRuntime::ResourceReference& reference) {
        auto& artifacts=Manifests.at(reference.Guid).Assets;
        const auto found=std::find_if(artifacts.begin(),artifacts.end(),[&](const auto& artifact){return artifact.LocalID==reference.LocalID;});
        Check(found!=artifacts.end(),"Missing test artifact");return *found;
    }
    void Put(const GameSproutRuntime::ResourceReference& reference,eAssetType type,const QByteArray& bytes,
        const std::string& descriptor="{}") {
        auto& manifest=Manifests[reference.Guid];manifest.GuidString=reference.Guid;
        AssetArtifact artifact;artifact.LocalID=reference.LocalID;artifact.Type=type;
        artifact.DataVersion=AssetArtifactIO::CurrentDataVersion(type);artifact.DataSize=bytes.size();
        artifact.Hash=AssetArtifactIO::Hash(bytes);artifact.DescriptorJson=descriptor;
        const auto path=QDir(QString::fromStdString(Root)).filePath(QString::fromStdString(reference.Guid+"_"+std::to_string(reference.LocalID)+".gsasset"));
        QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write isolated artifact");
        Check(file.write(bytes)==bytes.size(),"Incomplete isolated artifact");file.close();
        std::erase_if(manifest.Assets,[&](const auto& entry){return entry.LocalID==reference.LocalID;});
        manifest.Assets.push_back(artifact);
        Prepared.erase({reference.Guid,reference.LocalID});
        Prepared.emplace(std::piecewise_construct,std::forward_as_tuple(reference.Guid,reference.LocalID),
            std::forward_as_tuple(reference.Guid,artifact,path.toStdString()));
    }
    void Material(const GameSproutRuntime::ResourceReference& reference,const Json::Value& document) {
        QByteArray bytes;Check(AssetValueBinary::Encode(document,bytes),"Cannot encode material");
        Put(reference,eAssetType::Material,bytes,R"({"Encoding":"BinaryValueLE1"})");
    }
    void Erase(const GameSproutRuntime::ResourceReference& reference) {
        std::erase_if(Manifests.at(reference.Guid).Assets,[&](const auto& artifact){return artifact.LocalID==reference.LocalID;});
        Prepared.erase({reference.Guid,reference.LocalID});
    }
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Assets final:public IAssetsManager {
public:
    std::vector<IAssetListener*> Listeners;
    IAssetMeta* QueryAsset(const Guid&) const override{return nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,std::uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterAssetListener(IAssetListener* listener) override{std::erase(Listeners,listener);}
    void Publish(const std::string& guid,eAssetImportState state=eAssetImportState::Ready,eAssetMetaEvent action=eAssetMetaEvent::Modify) {
        const AssetEvent event(action,Guid(guid),"Fixture",{},eAssetImporterType::Model,state,{});
        for(auto* listener:Listeners)switch(action) {
            case eAssetMetaEvent::Create:listener->OnAssetCreate(event);break;
            case eAssetMetaEvent::Modify:listener->OnAssetModify(event);break;
            case eAssetMetaEvent::Remove:listener->OnAssetRemove(event);break;
        }
    }
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
void Revisions(Assets& assets,Library& library) {
    const auto mesh=Reference(ModelGuid,10),material=Reference(ModelGuid,20),texture=Reference(TextureGuid,30);
    library.Put(mesh,eAssetType::Mesh,"mesh");library.Put(material,eAssetType::Material,"material");
    library.Put(texture,eAssetType::Texture,"texture");
    LibraryRuntimeResources provider(&assets,&library);
    unsigned notifications=0;provider.ResourcesChanged=[&]{++notifications;};
    Check(library.ManifestReads==0,"Provider eagerly read manifests");
    const auto initial=provider.GetRevision();assets.Publish(UnobservedGuid);
    Check(library.ManifestReads==0&&provider.GetRevision()==initial&&notifications==0,"Unobserved source invalidated resources");
    const auto meshRevision=provider.GetResourceRevision(mesh),materialRevision=provider.GetResourceRevision(material);
    const auto textureRevision=provider.GetResourceRevision(texture);
    Check(library.ManifestReads==2&&meshRevision&&materialRevision&&textureRevision,"Observation did not read each requested GUID once");
    const auto reads=library.ManifestReads,assetReads=library.AssetReads;
    for(unsigned index=0;index<10000;++index) {
        Check(provider.GetResourceRevision(mesh)==meshRevision,"Stable resource changed without a publication");
        provider.GetResourceRevision(material);provider.GetResourceRevision(texture);
    }
    Check(library.ManifestReads==reads&&library.AssetReads==assetReads,"Resource version hot path queried Library");
    library.Manifests.at(ModelGuid).Generation="another-publication";
    library.Manifests.at(ModelGuid).SourcePath="Renamed.fbx";
    library.Artifact(mesh).Name="Renamed subasset";
    assets.Publish(ModelGuid);assets.Publish(ModelGuid,eAssetImportState::Pending);assets.Publish(ModelGuid,eAssetImportState::Failed);
    Check(provider.GetRevision()==initial&&provider.GetResourceRevision(mesh)==meshRevision&&notifications==0,"Cache hit, rename or failed pending import invalidated unchanged published bytes");
    library.Artifact(mesh).Hash="changed-mesh";assets.Publish(ModelGuid);
    auto changed=provider.GetResourceRevision(mesh);Check(changed!=meshRevision,"Changed payload did not invalidate");
    Check(notifications==1,"Effective resource change did not emit one wake-up notification");
    Check(provider.GetResourceRevision(material)==materialRevision&&provider.GetResourceRevision(texture)==textureRevision,"One subasset invalidated siblings or an unrelated GUID");
    const auto global=provider.GetRevision();assets.Publish(ModelGuid);
    Check(provider.GetRevision()==global&&provider.GetResourceRevision(mesh)==changed&&notifications==1,"Duplicate publication changed versions or woke idle views");
    auto checkField=[&](const char* field) {
        assets.Publish(ModelGuid);const auto next=provider.GetResourceRevision(mesh);
        Check(next!=changed,std::string("Signature ignored ")+field);changed=next;
        Check(provider.GetResourceRevision(material)==materialRevision,"Signature edit invalidated sibling");
    };
    library.Artifact(mesh).DataVersion++;checkField("DataVersion");
    library.Artifact(mesh).DataSize++;checkField("DataSize");
    library.Artifact(mesh).DescriptorJson=R"({"Layout":"changed"})";checkField("DescriptorJson");
    library.Artifact(mesh).Type=eAssetType::Texture;checkField("Type");
    library.Erase(mesh);assets.Publish(ModelGuid);
    const auto missing=provider.GetResourceRevision(mesh);Check(missing!=changed,"Removed LocalID retained its old version");
    assets.Publish(ModelGuid);Check(provider.GetResourceRevision(mesh)==missing,"Repeated Missing notification changed version");
    library.Put(mesh,eAssetType::Mesh,"restored");assets.Publish(ModelGuid);
    Check(provider.GetResourceRevision(mesh)!=missing,"Restored LocalID retained a cached failure");
    const auto newResource=Reference(ModelGuid,999);
    const auto missingNew=provider.GetResourceRevision(newResource);
    library.Put(newResource,eAssetType::Mesh,"new");assets.Publish(ModelGuid,eAssetImportState::Ready,eAssetMetaEvent::Create);
    Check(provider.GetResourceRevision(newResource)!=missingNew,"First publication did not invalidate observed Missing");
    const auto beforeRemove=provider.GetResourceRevision(material);
    library.Manifests.erase(ModelGuid);assets.Publish(ModelGuid,eAssetImportState::NotRequired,eAssetMetaEvent::Remove);
    Check(provider.GetResourceRevision(material)!=beforeRemove,"Whole source removal did not invalidate its subresources");
}
void MaterialDependencies(Assets& assets,Library& library) {
    const auto alias=Reference(ModelGuid,50),target=Reference(MaterialGuid,1),other=Reference(OtherMaterialGuid,1);
    const auto shader=Reference(ShaderGuid,1),texture=Reference(TextureGuid,1),sibling=Reference(ModelGuid,60);
    Json::Value source(Json::objectValue);source["ExternalMaterial"]=JsonReference(target.Guid,target.LocalID);
    library.Material(alias,source);
    Json::Value body(Json::objectValue);body["Shader"]=JsonReference(shader.Guid,shader.LocalID);
    body["BaseColorTexture"]=JsonReference(texture.Guid,texture.LocalID);library.Material(target,body);
    Json::Value descriptor(Json::objectValue);descriptor["Format"]="GameSproutShader";descriptor["ShaderVersion"]=1;
    descriptor["Material"]["Name"]="Isolated Shader";descriptor["Material"]["Properties"]=Json::Value(Json::arrayValue);
    descriptor["Passes"]=Json::Value(Json::arrayValue);
    library.Put(shader,eAssetType::Shader,"shader",AssetArtifactIO::JsonText(descriptor));
    library.Put(texture,eAssetType::Texture,"texture");library.Put(sibling,eAssetType::Mesh,"sibling");
    LibraryRuntimeResources provider(&assets,&library);std::string error;GameSproutRuntime::MaterialRenderData data;
    Check(provider.LoadMaterial(alias,data,error),error);
    auto revision=provider.GetResourceRevision(alias),targetRevision=provider.GetResourceRevision(target);
    const auto siblingRevision=provider.GetResourceRevision(sibling);
    Check(data.Shader==shader&&data.BaseColorTexture==texture,"Alias did not decode its target");
    // Shader descriptor decoding is part of material loading, even when no shader
    // bytecode LoadShader call has happened. Its changes must invalidate aliases.
    library.Artifact(shader).DescriptorJson+=" ";assets.Publish(shader.Guid);
    Check(provider.GetResourceRevision(alias)!=revision,"Shader normalization dependency did not invalidate alias");
    Check(provider.GetResourceRevision(target)==targetRevision,"Unloaded target gained an invented dependency");
    revision=provider.GetResourceRevision(alias);
    library.Artifact(target).Hash="new-target";assets.Publish(target.Guid);
    Check(provider.GetResourceRevision(alias)!=revision&&provider.GetResourceRevision(sibling)==siblingRevision,"Target material change missed alias or invalidated unrelated mesh");
    revision=provider.GetResourceRevision(alias);provider.GetResourceRevision(texture);
    library.Artifact(texture).Hash="new-texture";assets.Publish(texture.Guid);
    Check(provider.GetResourceRevision(alias)==revision,"Texture binding was incorrectly made a material decode dependency");
    library.Erase(shader);assets.Publish(shader.Guid);
    revision=provider.GetResourceRevision(alias);
    Check(!provider.LoadMaterial(alias,data,error),"Missing Shader retained a decoded alias");
    library.Put(shader,eAssetType::Shader,"restored shader",AssetArtifactIO::JsonText(descriptor));assets.Publish(shader.Guid);
    Check(provider.GetResourceRevision(alias)!=revision&&provider.LoadMaterial(alias,data,error),"Restoring Shader did not recover material normalization");
    library.Erase(target);assets.Publish(target.Guid);
    revision=provider.GetResourceRevision(alias);Check(!provider.LoadMaterial(alias,data,error),"Missing alias target loaded stale material");
    library.Material(target,body);assets.Publish(target.Guid);
    Check(provider.GetResourceRevision(alias)!=revision&&provider.LoadMaterial(alias,data,error),"Restored alias target did not recover");
    // Loading a new mapping drops its old dependency edges.
    Json::Value plain(Json::objectValue);plain["Shader"]="GameSprout/Preview";library.Material(other,plain);
    source["ExternalMaterial"]=JsonReference(other.Guid,other.LocalID);library.Material(alias,source);assets.Publish(alias.Guid);
    Check(provider.LoadMaterial(alias,data,error),error);revision=provider.GetResourceRevision(alias);
    library.Artifact(target).Hash="old-target-now-unrelated";assets.Publish(target.Guid);
    library.Artifact(shader).Hash="old-shader-now-unrelated";assets.Publish(shader.Guid);
    Check(provider.GetResourceRevision(alias)==revision,"Old alias dependencies survived a mapping replacement");
    library.Artifact(other).Hash="current-target";assets.Publish(other.Guid);
    Check(provider.GetResourceRevision(alias)!=revision,"New alias dependency was not registered");
}
void ArtifactRebuild(Assets& assets,Library& library) {
    const auto repaired=Reference(ModelGuid,70),healthy=Reference(ModelGuid,80),alias=Reference(MaterialGuid,1);
    Json::Value body(Json::objectValue);body["Shader"]="GameSprout/Preview";
    library.Material(repaired,body);library.Material(healthy,body);
    library.Manifests.at(ModelGuid).Generation="original-publication";
    Json::Value mapping(Json::objectValue);mapping["ExternalMaterial"]=JsonReference(repaired.Guid,repaired.LocalID);
    library.Material(alias,mapping);
    LibraryRuntimeResources provider(&assets,&library);std::string error;GameSproutRuntime::MaterialRenderData data;
    Check(provider.LoadMaterial(alias,data,error)&&provider.LoadMaterial(healthy,data,error),error);
    const auto initial=provider.GetResourceRevision(repaired),healthyRevision=provider.GetResourceRevision(healthy);
    const auto aliasRevision=provider.GetResourceRevision(alias);
    const auto signature=library.Artifact(repaired);
    const auto path=library.Prepared.at({repaired.Guid,repaired.LocalID}).Path;
    Check(QFile::remove(QString::fromStdString(path)),"Cannot remove isolated Artifact");
    Check(!provider.LoadMaterial(alias,data,error),"Missing Artifact did not fail material reading");
    Check(provider.GetResourceRevision(repaired)==initial,"Read failure itself changed the published resource version");
    // The normal pipeline rebuild restores exactly the same content/signature.
    library.Material(repaired,body);library.Manifests.at(ModelGuid).Generation="rebuilt-publication";
    Check(library.Artifact(repaired).Hash==signature.Hash&&library.Artifact(repaired).DescriptorJson==signature.DescriptorJson,
        "Rebuild fixture unexpectedly changed its signature");
    assets.Publish(ModelGuid,eAssetImportState::Pending);assets.Publish(ModelGuid,eAssetImportState::Failed);
    Check(provider.GetResourceRevision(repaired)==initial,"Pending/failed notification retried an unpublished rebuild");
    assets.Publish(ModelGuid);
    const auto restored=provider.GetResourceRevision(repaired);
    Check(restored!=initial&&provider.GetResourceRevision(alias)!=aliasRevision,"Same-content Artifact rebuild retained cached failure or alias failure");
    Check(provider.GetResourceRevision(healthy)==healthyRevision,"Rebuild invalidated a healthy sibling Artifact");
    Check(provider.LoadMaterial(alias,data,error),error);
    const auto restoredAlias=provider.GetResourceRevision(alias),global=provider.GetRevision();assets.Publish(ModelGuid);
    Check(provider.GetRevision()==global&&provider.GetResourceRevision(repaired)==restored&&provider.GetResourceRevision(alias)==restoredAlias,
        "Duplicate Ready notification invalidated repaired resources again");
    // If the retry also fails, replaying the same publication must not create
    // an endless invalidate/reload loop; a later real publication can retry it.
    Check(QFile::remove(QString::fromStdString(path))&&!provider.LoadMaterial(alias,data,error),"Cannot stage a second read failure");
    assets.Publish(ModelGuid);
    Check(provider.GetResourceRevision(repaired)==restored,"Same publication repeatedly retried failed Artifact");
    library.Material(repaired,body);library.Manifests.at(ModelGuid).Generation="next-rebuilt-publication";assets.Publish(ModelGuid);
    Check(provider.GetResourceRevision(repaired)!=restored&&provider.GetResourceRevision(healthy)==healthyRevision&&
        provider.LoadMaterial(alias,data,error),"A later same-content rebuild did not recover the failed reader");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"No isolated fixture directory");
        Assets assets;Library library;library.Root=temporary.path().toStdString();
        Revisions(assets,library);Check(assets.Listeners.empty(),"Revision provider retained event listener");
        library.Manifests.clear();library.Prepared.clear();
        MaterialDependencies(assets,library);Check(assets.Listeners.empty(),"Material provider retained event listener");
        library.Manifests.clear();library.Prepared.clear();
        ArtifactRebuild(assets,library);Check(assets.Listeners.empty(),"Artifact rebuild provider retained event listener");
        std::cout<<"PASS per-subresource publication revisions, memory-only reads, Missing recovery, material/shader dependencies and same-content Artifact repair\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}

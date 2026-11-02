#include "LibraryRuntimeResources.h"
#include "ILibraryManager.h"
#include "PhysicsMaterialDefinition.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void Write(const QString& path,const QByteArray& bytes) {
    QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write isolated fixture");
    Check(file.write(bytes)==bytes.size(),"Cannot write complete fixture");
}
class Asset final:public IAsset {
public:
    Guid ID{"1234567890abcdef1234567890abcdef"};
    eAssetType Type=eAssetType::PhysicsMaterial;
    std::string Path,Name="Surface";
    std::string Descriptor=R"({"Encoding":"RawBytes","Format":"PhysicsMaterial1","Layout":"F32F32U32U32LE"})";
    std::uint32_t Version=2;
    std::uint64_t Size=16;
    const Guid& GetGuid() const override{return ID;}
    uint64_t GetLocalID() const override{return 17;}
    eAssetType GetAssetType() const override{return Type;}
    const std::string& GetName() const override{return Name;}
    const std::string& GetArtifactPath() const override{return Path;}
    const std::string& GetDataDescriptorJson() const override{return Descriptor;}
    uint32_t GetDataVersion() const override{return Version;}
    uint64_t GetDataSize() const override{return Size;}
};
class Library final:public ILibraryManager {
public:
    Asset Prepared;bool Present=true;std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&) const override{return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid& guid,uint64_t localID) override {
        return Present&&guid.GetGuidString()==Prepared.ID.GetGuidString()&&localID==17?&Prepared:nullptr;
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
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterAssetListener(IAssetListener* listener) override{std::erase(Listeners,listener);}
    void Publish(const Guid& guid,bool removed) {
        const AssetEvent event(removed?eAssetMetaEvent::Remove:eAssetMetaEvent::Create,guid,"Surface.physicmaterial",{},
            eAssetImporterType::PhysicsMaterial,eAssetImportState::Ready,{});
        for(auto* listener:Listeners)if(removed)listener->OnAssetRemove(event);else listener->OnAssetCreate(event);
    }
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class ObservedProvider final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    explicit ObservedProvider(LibraryRuntimeResources& resources):Resources(resources){}
    LibraryRuntimeResources& Resources;unsigned Reads=0;float Friction=-1;
    std::uint64_t GetRevision() const override{return Resources.GetRevision();}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string&) override{return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&) override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&) override{return false;}
    bool LoadPhysicsMaterial(const GameSproutRuntime::ResourceReference& reference,
        GameSproutRuntime::PhysicsMaterialData& data,std::string& error) override {
        ++Reads;const bool result=Resources.LoadPhysicsMaterial(reference,data,error);if(result)Friction=data.Friction;return result;
    }
};
struct RuntimeOwner {
    GameSproutRuntime::IGameSproutRuntime* Pointer=nullptr;
    ~RuntimeOwner(){if(Pointer){Pointer->SetResourceProvider(nullptr);delete Pointer;}}
};
void Surface(Library& library,float friction) {
    GameSproutRuntime::PhysicsMaterialData data;data.Friction=friction;
    std::vector<std::uint8_t> bytes;std::string error;
    Check(PhysicsMaterialDefinition::Encode(data,bytes,error),error);
    Write(QString::fromStdString(library.Prepared.Path),QByteArray(reinterpret_cast<const char*>(bytes.data()),static_cast<qsizetype>(bytes.size())));
}
void Steps(GameSproutRuntime::IGameSproutRuntime& runtime,unsigned count){for(unsigned i=0;i<count;++i)runtime.FixedUpdate(1.f/60);}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        Check(argc==2,"Pass the independent Runtime directory");QTemporaryDir temporary;Check(temporary.isValid(),"No isolated project");
        Assets assets;Library library;library.Root=temporary.path().toStdString();
        library.Prepared.Path=temporary.filePath("Surface.gsasset").toStdString();Surface(library,.1f);
        const auto reference=GameSproutRuntime::ResourceReference{library.Prepared.ID.GetGuidString(),17};
        {
            LibraryRuntimeResources resources(&assets,&library);std::string error;GameSproutRuntime::PhysicsMaterialData data;
            Check(resources.LoadPhysicsMaterial(reference,data,error)&&std::fabs(data.Friction-.1f)<.0001f,error);
            Check(!resources.LoadPhysicsMaterial({"invalid",17},data,error)&&!error.empty(),"Invalid identity silently used a default material");
            Check(!resources.LoadPhysicsMaterial({reference.Guid,0},data,error)&&!error.empty(),"Zero LocalID silently used a default material");
            const auto descriptor=library.Prepared.Descriptor;
            library.Prepared.Descriptor="{";Check(!resources.LoadPhysicsMaterial(reference,data,error),"Corrupt descriptor silently used a default material");
            library.Prepared.Descriptor=descriptor;library.Prepared.Type=eAssetType::Material;
            Check(!resources.LoadPhysicsMaterial(reference,data,error),"Wrong resource type silently used a default material");
            library.Prepared.Type=eAssetType::PhysicsMaterial;library.Prepared.Version=1;
            Check(!resources.LoadPhysicsMaterial(reference,data,error),"Invalid data version silently used a default material");
            library.Prepared.Version=2;Write(QString::fromStdString(library.Prepared.Path),"truncated");
            Check(!resources.LoadPhysicsMaterial(reference,data,error),"Corrupt artifact length silently used a default material");
            Write(QString::fromStdString(library.Prepared.Path),QByteArray(16,'\xff'));
            Check(!resources.LoadPhysicsMaterial(reference,data,error),"Corrupt current-format bytes silently used a default material");
            Check(QFile::remove(QString::fromStdString(library.Prepared.Path)),"Cannot remove test artifact");
            Check(!resources.LoadPhysicsMaterial(reference,data,error),"Present descriptor with a missing artifact silently used a default material");
            Surface(library,.1f);
            Check(resources.LoadPhysicsMaterial({reference.Guid,999},data,error)&&error.empty()&&data.Friction==.6f,
                  "A genuinely missing LocalID did not use the default surface");
            ObservedProvider provider(resources);RuntimeOwner owner{Export(argv[1])};auto* runtime=owner.Pointer;
            Check(runtime,"Cannot load current Runtime");runtime->SetResourceProvider(&provider);
            auto* scene=runtime->CreateScene("Missing physics material");auto* ground=scene->CreateGameObject("Ground");
            ground->GetTransform()->SetLocalPosition({0,-.5f,0});auto* collider=ground->AddComponent<GameSproutRuntime::IBoxColliderComponent>();
            Check(collider->SetShape({{20,1,20}}),"Cannot configure ground");auto settings=collider->GetColliderSettings();settings.Material=reference;
            Check(collider->SetColliderSettings(settings),"Cannot assign material reference");
            auto* object=scene->CreateGameObject("Falling box");object->GetTransform()->SetLocalPosition({0,3,0});
            object->AddComponent<GameSproutRuntime::IBoxColliderComponent>();auto* body=object->AddComponent<GameSproutRuntime::IRigidbodyComponent>();
            unsigned collisions=0;collider->RegisterCollisionEnter([&](const auto&){++collisions;});
            Check(runtime->BeginExecution(error),error);Steps(*runtime,120);
            Check(provider.Reads==1&&std::fabs(provider.Friction-.1f)<.0001f&&collisions==1,"Initial surface was not cached during simulation");
            const QString source=temporary.filePath("Surface.physicmaterial");Write(source,"original source");
            Check(QFile::remove(source),"Cannot delete isolated source");library.Present=false;
            const auto oldRevision=resources.GetRevision();assets.Publish(library.Prepared.ID,true);
            Check(resources.GetRevision()!=oldRevision&&body->Teleport({0,3,0},{},true)&&runtime->SynchronizePhysics(error),error);
            Check(provider.Reads==2&&provider.Friction==.6f&&collider->GetColliderSettings().Material==reference,
                  "Deletion kept cached old material or erased the stable Missing reference");
            Steps(*runtime,120);Check(collisions>=2&&object->GetTransform()->GetLocalPosition().Y>.45f&&
                  object->GetTransform()->GetLocalPosition().Y<.55f,"Missing material stopped simulation or disabled collision");
            Write(source,"restored source, same GUID");Surface(library,.9f);library.Present=true;assets.Publish(library.Prepared.ID,false);
            Check(body->Teleport({0,3,0},{},true)&&runtime->SynchronizePhysics(error),error);
            Check(provider.Reads==3&&std::fabs(provider.Friction-.9f)<.0001f&&collider->GetColliderSettings().Material==reference,
                  "Restoring the same identity retained the cached missing default");
            Steps(*runtime,120);Check(collisions>=3&&object->GetTransform()->GetLocalPosition().Y>.45f,
                                      "Restored material did not continue colliding and simulating");
            Check(runtime->EndExecution(error),error);
        }
        Check(assets.Listeners.empty(),"Provider retained a released event listener");
        std::cout<<"PASS missing PhysicsMaterial fallback, corruption errors, cache invalidation, simulation and GUID restoration\n";return 0;
    } catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}

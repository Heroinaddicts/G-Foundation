#include "AssetTestSupport.h"
#include "AssetImportJob.h"
#include "GameSproutRuntimeExport.h"
#include "../../Module/Shared/NativeMeshDefinition.h"
#include "../../Module/RuntimeAssets/LibraryRuntimeResources.h"
#include <QtEndian>
#include <bit>
#include <cstring>
#include <limits>

namespace {
void Put(QByteArray& bytes,std::uint32_t value) {char storage[4];qToLittleEndian(value,reinterpret_cast<uchar*>(storage));bytes.append(storage,4);}
void PutFloat(QByteArray& bytes,float value) {Put(bytes,std::bit_cast<std::uint32_t>(value));}
void Replace(QByteArray& bytes,std::size_t offset,std::uint32_t value) {qToLittleEndian(value,reinterpret_cast<uchar*>(bytes.data()+offset));}
float Float(const QByteArray& bytes,std::size_t offset) {return std::bit_cast<float>(qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar*>(bytes.constData()+offset)));}
QByteArray Mesh(std::uint32_t flags=NativeMeshDefinition::ValidFlags,bool degenerate=false,bool allDegenerate=false) {
    QByteArray bytes(NativeMeshDefinition::Magic.data(),NativeMeshDefinition::Magic.size());
    Put(bytes,NativeMeshDefinition::Version);Put(bytes,flags);Put(bytes,4);Put(bytes,degenerate?9:6);Put(bytes,2);Put(bytes,5);
    bytes.append("Panel",5);while(bytes.size()%4)bytes.append('\0');
    for(float value:{-1.f,-1.f,2.f,1.f,-1.f,2.f,1.f,1.f,2.f,-1.f,1.f,2.f})PutFloat(bytes,value);
    if(flags&NativeMeshDefinition::HasNormals)for(int i=0;i<4;++i)for(float value:{0.f,0.f,2.f})PutFloat(bytes,value);
    if(flags&NativeMeshDefinition::HasTangents)for(int i=0;i<4;++i)for(float value:{1.f,0.f,0.f,-1.f})PutFloat(bytes,value);
    if(flags&NativeMeshDefinition::HasUV0)for(float value:{0.f,0.f,1.f,0.f,1.f,1.f,0.f,1.f})PutFloat(bytes,value);
    if(flags&NativeMeshDefinition::HasColors)for(int i=0;i<4;++i)for(float value:{1.f,.5f,.25f,1.f})PutFloat(bytes,value);
    for(auto index:{0u,1u,2u,0u,2u,3u})Put(bytes,allDegenerate?0:index);
    if(degenerate)for(auto index:{0u,0u,1u})Put(bytes,index);
    Put(bytes,0);Put(bytes,3);Put(bytes,1);
    Put(bytes,3);Put(bytes,degenerate?6:3);Put(bytes,7);
    return bytes;
}
QByteArray Tetrahedron() {
    QByteArray bytes(NativeMeshDefinition::Magic.data(), NativeMeshDefinition::Magic.size());
    for (auto value : {NativeMeshDefinition::Version, 0u, 4u, 12u, 1u, 4u}) Put(bytes, value);
    bytes.append("Hull", 4);
    for (float value : {0.f,0.f,0.f, 1.f,0.f,0.f, 0.f,1.f,0.f, 0.f,0.f,1.f}) PutFloat(bytes, value);
    for (auto value : {0u,2u,1u, 0u,1u,3u, 0u,3u,2u, 1u,2u,3u, 0u,12u,0u}) Put(bytes, value);
    return bytes;
}
class Context final : public Foundation::ITaskContext {
public:
    bool Cancel=false;
    UInt64 GetTaskId() const override {return 1;}
    bool IsCancellationRequested() const override {return Cancel;}
    void* GetContext() const override {return nullptr;}
    bool SetOwnedContext(void*,Foundation::fTaskOwnedContextRelease) override {return false;}
};
struct Imported {AssetImportResult Result;QByteArray Bytes;};
class Asset final : public IAsset {
public:
    explicit Asset(const Guid& guid):ID(guid) {}
    Guid ID;AssetArtifact Data;std::string Path;
    const Guid& GetGuid() const override {return ID;}
    uint64_t GetLocalID() const override {return Data.LocalID;}
    eAssetType GetAssetType() const override {return Data.Type;}
    const std::string& GetName() const override {return Data.Name;}
    const std::string& GetArtifactPath() const override {return Path;}
    const std::string& GetDataDescriptorJson() const override {return Data.DescriptorJson;}
    uint32_t GetDataVersion() const override {return Data.DataVersion;}
    uint64_t GetDataSize() const override {return Data.DataSize;}
};
class Library final : public ILibraryManager {
public:
    Asset* Item=nullptr;std::string Root;
    ILDBSession* GetLDBSession() const override {return nullptr;}
    const std::string& GetLibraryRoot() const override {return Root;}
    std::vector<std::string> GetManifestGuids() const override {return {};}
    bool GetManifest(const Guid&,AssetImportManifest&) const override {return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override {return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override {return false;}
    std::vector<std::string> FindDependents(const std::string&) const override {return {};}
    IAsset* QueryAsset(const Guid& guid,uint64_t id) override {return Item&&guid.GetGuidString()==Item->ID.GetGuidString()&&id==Item->Data.LocalID?Item:nullptr;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};
class Fixture {
public:
    QTemporaryDir Temporary;
    Guid ID{100,200,300,400};
    GameSproutRuntime::IGameSproutRuntime * const Runtime = Export(TEST_BINARY_ROOT);
    ~Fixture() { delete Runtime; }
    Imported Import(const QByteArray& source,Json::Value settings=Json::Value(Json::objectValue)) {
        Check(Temporary.isValid(),"Temporary project unavailable");QDir root(Temporary.path());Check(root.mkpath("Assets"),"Cannot create Assets");
        const QString path=root.filePath("Assets/Panel.mesh");std::string error;
        Check(AssetArtifactIO::Write(path,source,error),error);
        TestMeta meta(eAssetImporterType::Mesh,ID,"Panel.mesh");meta.Settings=settings;
        meta.Identities="[{\"LocalID\":1,\"Type\":3,\"SourceKey\":\"main\",\"Name\":\"Panel\"}]";
        AssetImportRequest request;request.Importer=eAssetImporterType::Mesh;request.GuidString=ID.GetGuidString();
        request.AssetsRoot=root.filePath("Assets").toStdString();request.LibraryRoot=root.filePath("Library").toStdString();
        request.MeshCollision=Runtime->GetMeshCollisionCooker();
        request.SourcePath="Panel.mesh";Check(meta.SerializeMeta(request.MetaJson),"Cannot serialize native mesh meta");
        AssetImportJob job(request);Context context;const bool ok=RunAssetImport(job,context);
        Check(ok==job.Result.Success,"Import function/result disagree");
        Imported result;result.Result=job.Result;
        if(ok) {
            Check(job.Result.Manifest.Assets.size()==1,"Native mesh produced unexpected resources");
            const auto& artifact=job.Result.Manifest.Assets[0];
            QFile file(QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(artifact.Path)));
            Check(file.open(QIODevice::ReadOnly),"Missing native mesh artifact");result.Bytes=file.readAll();
        }
        return result;
    }
    GameSproutRuntime::MeshRenderData Decode(const Imported& imported,bool expectedSuccess=true) {
        Check(imported.Result.Success,imported.Result.Error);Asset item(ID);item.Data=imported.Result.Manifest.Assets[0];
        item.Path=QDir(Temporary.path()).filePath("prepared.gsasset").toStdString();std::string error;
        Check(AssetArtifactIO::Write(QString::fromStdString(item.Path),imported.Bytes,error),error);
        Library library;library.Item=&item;LibraryRuntimeResources resources(nullptr,&library);GameSproutRuntime::MeshRenderData mesh;
        const bool decoded=resources.LoadMesh({ID.GetGuidString(),1},mesh,error);
        Check(decoded==expectedSuccess,expectedSuccess?"Native artifact is not Runtime-readable: "+error:"Old coordinate payload was accepted");
        if(!decoded)Check(!error.empty(),"Rejected coordinate payload has no diagnostic");return mesh;
    }
};
}

int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    try {
        Fixture fixture;
        const auto cooker=fixture.Runtime ? fixture.Runtime->GetMeshCollisionCooker() : GameSproutRuntime::MeshCollisionCooker{};
        Check(cooker.Cook&&cooker.Format,"Cannot load Runtime mesh cooker");
        const auto source=Mesh();const auto imported=fixture.Import(source);
        Check(imported.Result.Success,imported.Result.Error);
        const auto& artifact=imported.Result.Manifest.Assets[0];
        Check(artifact.LocalID==1&&artifact.SourceKey=="main"&&artifact.Type==eAssetType::Mesh,"Explicit main LocalID 1 was not retained");
        Json::Value descriptor;Check(AssetArtifactIO::Parse(artifact.DescriptorJson,descriptor),"Cannot decode native descriptor");
        const auto decoded=fixture.Decode(imported);
        Check(descriptor["Encoding"]=="RawBytes"&&descriptor["VertexStride"].asUInt()==64&&
            descriptor["IndexOffset"].asUInt64()==256&&decoded.Bytes.size()==280,"Prepared bytes have the wrong headerless GPU layout");
        Check(artifact.DataVersion==4,"Native prepared mesh does not identify the collision-enabled layout");
        Check(imported.Result.Manifest.ToolchainHash==std::to_string(cooker.Format),"Manifest did not record the Jolt cook format");
        auto oldMesh=imported;oldMesh.Result.Manifest.Assets[0].DataVersion=3;
        fixture.Decode(oldMesh,false);
        Check(decoded.VertexCount==4&&decoded.IndexCount==6&&decoded.Sections.size()==2&&decoded.Sections[0].MaterialSlot==1&&decoded.Sections[1].MaterialSlot==7,
            "Native submesh slots/indices were changed");
        Check(Float(imported.Bytes,8)==2&&Float(imported.Bytes,20)==2&&decoded.LocalBounds.Center.Z==2&&decoded.LocalBounds.Extents.X==1,
            "Native Unity position/normal values were converted again or bounds changed");
        const auto tetrahedron=fixture.Import(Tetrahedron());
        const auto tetraRender=fixture.Decode(tetrahedron);
        Json::Value tetraLayout;
        Check(AssetArtifactIO::Parse(tetrahedron.Result.Manifest.Assets[0].DescriptorJson,tetraLayout),"Cannot read tetrahedron layout");
        const auto& triangle=tetraLayout["Collision"]["Triangle"];
        const auto& convex=tetraLayout["Collision"]["Convex"];
        Check(triangle["Offset"].asUInt64()==tetraRender.Bytes.size()&&triangle["Size"].asUInt64()>0&&
            convex["Offset"].asUInt64()==triangle["Offset"].asUInt64()+triangle["Size"].asUInt64()&&convex["Size"].asUInt64()>0&&
            convex["Offset"].asUInt64()+convex["Size"].asUInt64()==std::uint64_t(tetrahedron.Bytes.size()),
            "Native mesh did not append tightly packed triangle and convex Jolt payloads");
        Check(tetraRender.Bytes.size()==144&&std::memcmp(tetraRender.Bytes.data(),tetrahedron.Bytes.constData(),144)==0,
            "Runtime did not isolate the native mesh GPU payload from the baked collision bytes");
        const auto collision=fixture.Import(Mesh(0));Check(collision.Result.Success,collision.Result.Error);const auto collisionMesh=fixture.Decode(collision);
        Check(collisionMesh.VertexStride==24&&collisionMesh.UVOffset==-1&&Float(collision.Bytes,20)==1,
            "Position-only collision mesh did not receive generated normals");
        const auto degenerate=fixture.Import(Mesh(NativeMeshDefinition::ValidFlags,true));
        Check(degenerate.Result.Success&&degenerate.Result.Diagnostics.find("1 degenerate")!=std::string::npos&&fixture.Decode(degenerate).IndexCount==6,
            "Degenerate removal lost submesh ranges or diagnostic");
        Check(!fixture.Import(Mesh(0,false,true)).Result.Success,"All-degenerate mesh was accepted");
        const auto reject=[&](QByteArray bad,const char* reason) {const auto result=fixture.Import(bad);Check(!result.Result.Success&&!result.Result.Error.empty(),reason);};
        auto bad=source;bad[0]='X';reject(bad,"Bad native magic accepted");
        bad=source;Replace(bad,8,1);reject(bad,"Old RH native version accepted");
        bad=source;bad[7]='1';Replace(bad,8,1);reject(bad,"Old RH native source header accepted");
        bad=source;Replace(bad,12,16);reject(bad,"Unknown native flag accepted");
        bad=source;bad.chop(1);reject(bad,"Truncated native body accepted");
        bad=source;bad.append('x');reject(bad,"Trailing native body accepted");
        bad=source;Replace(bad,40,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));reject(bad,"Nonfinite vertex accepted");
        bad=source;Replace(bad,32+5,1);reject(bad,"Nonzero name padding accepted");
        bad=source;bad[32]='\0';reject(bad,"NUL mesh name accepted");
        bad=source;Replace(bad,40+4*64,99);reject(bad,"Out-of-range vertex index accepted");
        bad=source;Replace(bad,bad.size()-24,3);reject(bad,"Overlapping submesh ranges accepted");
        bad=source;Replace(bad,bad.size()-4,1024);reject(bad,"Invalid material slot accepted");
        reject("{\"Vertices\":[]}","Removed JSON mesh format was accepted");
        Json::Value settings;settings["ScaleFactor"]=.01;Check(!fixture.Import(source,settings).Result.Success,"Second native unit conversion accepted");
        settings=Json::Value(Json::objectValue);settings["MeshCompression"]="High";Check(!fixture.Import(source,settings).Result.Success,"Unsupported native compression accepted");
        Check(fixture.Import(source).Result.Manifest.Assets[0].LocalID==1,"Native reimport changed identity");
        AssetImportRequest request;AssetImportJob cancelled(request);Context context;context.Cancel=true;cancelled.Context=&context;
        cancelled.SourceBytes=source;cancelled.Settings=Json::Value(Json::objectValue);
        Check(!ImportNativeMesh(cancelled)&&!cancelled.Result.Error.empty(),"Native decode ignored cancellation");
        std::cout<<"PASS native mesh binary import, Runtime decode, stable identity, collision geometry and strict validation\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

#include "AssetTestSupport.h"
#include "AssetImportJob.h"
#include "ModelTextureResolver.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <set>

namespace {
void WriteFile(const QString& path,const QByteArray& bytes) {
    Check(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create fixture parent");
    std::string error;
    Check(AssetArtifactIO::Write(path,bytes,error),"Cannot write fixture: "+error);
}
QByteArray ReadFile(const QString& path) {
    QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read "+path.toStdString());
    const auto bytes=file.readAll();Check(file.error()==QFileDevice::NoError,"Cannot read fixture bytes");return bytes;
}
void WriteMeta(const QString& path,const TestMeta& meta) {
    std::string json;Check(meta.SerializeMeta(json),"Cannot serialize fixture Meta");
    WriteFile(path+".meta",QByteArray::fromStdString(json));
}
void WriteOBJ(const QString& path,const std::string& references,bool twoMaterials=false) {
    WriteFile(path,"mtllib Materials/Model.mtl\no Geometry\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
        "vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\nusemtl Surface\nf 1/1 2/2 3/3\n"
        +QByteArray(twoMaterials?"usemtl Other\nf 2/2 4/4 3/3\n":""));
    WriteFile(QFileInfo(path).absolutePath()+"/Materials/Model.mtl",QByteArray::fromStdString(references));
}
class DirectContext final : public Foundation::ITaskContext {
public:
    UInt64 GetTaskId() const override {return 1;}
    bool IsCancellationRequested() const override {return false;}
    void* GetContext() const override {return nullptr;}
    bool SetOwnedContext(void*,Foundation::fTaskOwnedContextRelease) override {return false;}
};
struct CoreResult {
    AssetImportResult Import;
    std::vector<Json::Value> Materials;
};
class ModelFixture {
public:
    QTemporaryDir Temporary;
    QDir Root;
    TestIDE IDE;
    TestProject Project;
    TestConsole Console;
    TestEditor Editor;
    FoundationProxy Foundation;
    GameSproutEditor::GameSproutModuleApi Api;
    ILibraryManager* Library=nullptr;
    IAssetsPipeline* Pipeline=nullptr;
    Foundation::FoundationApi* Real=nullptr;
    uint32_t NextGuid=100;
    std::vector<GameSproutEditor::IGameSproutModule*> Initialized;

    explicit ModelFixture(Foundation::FoundationApi* real)
        :Root(Temporary.path()),Api(&Foundation,&Editor),Real(real) {
        Check(Temporary.isValid()&&Root.mkpath("Assets"),"Cannot create temporary Assets");
        Project.Root=Temporary.path().toStdString();Editor.IDE=&IDE;
        Editor.Modules["Project"]=&Project;Editor.Modules["Console"]=&Console;
        Foundation.Real=real;Foundation.Tasks.Real=real->TaskApi();Foundation.Tasks.IDE=&IDE;
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator"})
            Editor.Modules[name]=LoadModule(name);
        for(const auto& entry:Editor.Modules) GameSproutEditor::GameSproutEditorFinal::Before(entry.second,&Api);
        for(const auto& entry:Editor.Modules) {
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,&Api),"Initialize "+entry.first);
            Initialized.push_back(entry.second);
        }
        for(const auto& entry:Editor.Modules) if(entry.first!="AssetsManager")
            Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,&Api),"Launch "+entry.first);
        Library=static_cast<ILibraryManager*>(Editor.Modules["LibraryManager"]);
        Pipeline=static_cast<IAssetsPipeline*>(Editor.Modules["AssetsPipeline"]);
    }
    ~ModelFixture() {
        // Pending tasks must be cancelled before their modules and temporary project disappear.
        for(const std::string name:{"AssetsManager","AssetsPipeline","ShaderCompiler","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=Editor.Modules.find(name);
            if(found!=Editor.Modules.end()&&std::find(Initialized.begin(),Initialized.end(),found->second)!=Initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second,&Api);
        }
    }
    Guid NewGuid() {return Guid(++NextGuid,200,300,400);}
    QString Asset(const std::string& relative) const {return Root.filePath("Assets/"+QString::fromStdString(relative));}
    AssetImportResult Import(TestMeta& meta,bool force=false) {
        bool done=false;AssetImportResult result;const int submissions=Foundation.Tasks.Submissions;
        Pipeline->ImportAsset(&meta,[&](const AssetImportResult& value) {
            Check(std::this_thread::get_id()==IDE.Main,"Pipeline callback left owner thread");
            Check(IDE.Count>0,"Pipeline unloaded before its callback");result=value;done=true;
        },force);
        QElapsedTimer timer;timer.start();
        while(!done&&timer.elapsed()<30000) {Real->Update();QCoreApplication::processEvents();QThread::msleep(2);}
        Check(done,"Pipeline callback timeout");
        Check(Foundation.Tasks.Submissions==submissions+1,"Import did not use TaskApi");
        Check(IDE.Count==0&&IDE.Loads==IDE.Unloads,"Pipeline Loading/Unloading imbalance");
        if(result.Success)meta.Identities=result.Manifest.SubAssetsJson;
        return result;
    }
    CoreResult Core(TestMeta& meta) {
        AssetImportRequest request;request.Importer=meta._ImporterType;request.GuidString=meta._Guid.GetGuidString();
        request.SourcePath=meta.Path;request.AssetsRoot=Root.filePath("Assets").toStdString();
        request.LibraryRoot=Root.filePath("CoreLibrary").toStdString();meta.SerializeMeta(request.MetaJson);
        AssetImportJob job(request);DirectContext context;
        const bool succeeded=RunAssetImport(job,context);
        Check(succeeded==job.Result.Success,"Core return and result disagree");
        CoreResult result;result.Import=job.Result;
        if(succeeded) for(const auto& artifact:job.Result.Manifest.Assets) if(artifact.Type==eAssetType::Material) {
            Json::Value material;
            Check(AssetValueBinary::Decode(ReadFile(QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(artifact.Path))),material),"Invalid material binary");
            result.Materials.push_back(std::move(material));
        }
        return result;
    }
    std::vector<Json::Value> Materials(const AssetImportResult& result) const {
        std::vector<Json::Value> materials;
        for(const auto& artifact:result.Manifest.Assets) if(artifact.Type==eAssetType::Material)
            materials.push_back(ReadArtifact(Library->QueryAsset(Guid(result.Manifest.GuidString),artifact.LocalID)));
        return materials;
    }
    AssetImportResult Texture(TestMeta& meta) {
        Check(QDir().mkpath(QFileInfo(Asset(meta.Path)).absolutePath()),"Cannot create texture directory");
        QImage image(4,4,QImage::Format_RGBA8888);image.fill(QColor(110,150,210));
        Check(image.save(Asset(meta.Path)),"Cannot write PNG");
        const auto result=Import(meta);Check(result.Success,"Texture fixture import: "+result.Error);
        WriteMeta(Asset(meta.Path),meta);return result;
    }
};
std::set<std::string> TextureRefs(const std::vector<Json::Value>& materials) {
    std::set<std::string> references;
    for(const auto& material:materials) for(const auto& texture:material["Textures"]) {
        const auto& ref=texture["Asset"];
        Check(ref["Guid"].isString()&&Guid(ref["Guid"].asString()).IsValid()
            &&ref["LocalID"].isUInt64()&&ref["LocalID"].asUInt64()!=0,"Material contains a fabricated/invalid TextureRef");
        references.insert(ref["Guid"].asString()+"_"+std::to_string(ref["LocalID"].asUInt64()));
    }
    return references;
}
std::size_t UnresolvedCount(const std::vector<Json::Value>& materials) {
    std::size_t count=0;
    for(const auto& material:materials) for(const auto& missing:material["UnresolvedTextures"]) {
        Check(missing["Channel"].isString()&&missing["SourcePath"].isString()&&!missing["Reason"].asString().empty(),"Incomplete missing-texture descriptor");++count;
    }
    return count;
}
void CheckModel(const AssetImportResult& result) {
    Check(result.Success&&result.Error.empty(),"Model import failed: "+result.Error);
    std::size_t meshes=0,materials=0,models=0;
    for(const auto& artifact:result.Manifest.Assets) {
        meshes+=artifact.Type==eAssetType::Mesh;materials+=artifact.Type==eAssetType::Material;models+=artifact.Type==eAssetType::Model;
    }
    Check(meshes>0&&materials>0&&models==1,"Model lost mesh/material/model artifacts");
}
void CheckMissing(const AssetImportResult& result,const std::vector<Json::Value>& materials) {
    CheckModel(result);
    Check(!result.Diagnostics.empty()&&result.Manifest.Diagnostics==result.Diagnostics,"Nonfatal diagnostics were lost");
    Check(UnresolvedCount(materials)>0,"Missing texture was not recorded");
    Check(TextureRefs(materials).empty(),"Missing author texture became a fabricated valid binding");
    Check(!result.Manifest.TextureSearches.empty(),"Missing texture search was not recorded for cache invalidation");
    for(const auto& search:result.Manifest.TextureSearches)
        Check(search.ResolvedPath.empty()&&!search.Reason.empty()&&search.Filename.find('\\')==std::string::npos
            &&search.RelativeFilename.find('\\')==std::string::npos,"Search descriptor is incomplete or Windows path unnormalized");
}
void CheckResolver(ModelFixture& fixture) {
    const std::string source="Resolver/Models/Thing.obj";
    WriteFile(fixture.Asset(source),"o Empty\n");
    Check(NormalizeModelTexturePath("C:\\author\\Textures\\Color.png")=="C:/author/Textures/Color.png","Windows path normalization failed");
    const auto resolve=[&](const std::string& filename,const std::string& relative="") {
        return ResolveModelTexture(fixture.Root.filePath("Assets").toStdString(),source,filename,relative,{"Resolver/Models/Materials"});
    };
    WriteFile(fixture.Asset("Resolver/Models/Color.png"),"local");
    WriteFile(fixture.Asset("Resolver/Models/Thing.fbm/Color.png"),"fbm");
    WriteFile(fixture.Asset("Resolver/Models/Materials/Color.png"),"material");
    WriteFile(fixture.Asset("Resolver/Textures/Color.png"),"ancestor");
    Check(resolve("C:\\author\\Color.png").Path=="Resolver/Models/Color.png","Model-local basename did not win priority");
    Check(QFile::remove(fixture.Asset("Resolver/Models/Color.png")),"Cannot remove local candidate");
    Check(resolve("C:\\author\\Color.png").Path=="Resolver/Models/Thing.fbm/Color.png","FBM priority failed");
    Check(QFile::remove(fixture.Asset("Resolver/Models/Thing.fbm/Color.png")),"Cannot remove FBM candidate");
    Check(resolve("Color.png").Path=="Resolver/Models/Materials/Color.png","MTL directory priority failed");
    Check(QFile::remove(fixture.Asset("Resolver/Models/Materials/Color.png")),"Cannot remove MTL candidate");
    Check(resolve("Color.png").Path=="Resolver/Textures/Color.png","Ancestor Textures lookup failed");
    WriteFile(fixture.Asset("Resolver/Models/Paint/Relative.png"),"relative");
    Check(resolve("C:\\author\\Relative.png","..\\Paint\\Relative.png").Path=="Resolver/Models/Paint/Relative.png","MTL-relative Windows path lookup failed");
    WriteFile(fixture.Asset("Resolver/Elsewhere/Absolute.png"),"absolute");
    Check(resolve(fixture.Asset("Resolver/Elsewhere/Absolute.png").toStdString()).Path=="Resolver/Elsewhere/Absolute.png","In-Assets absolute fallback failed");
    WriteFile(fixture.Asset("Resolver/Models/Tree.png"),"same stem");
    const auto psd=resolve("C:\\author\\Tree.psd");
    Check(psd.Path.empty()&&!psd.Reason.empty(),"PSD was guessed to same-stem PNG");
    Check(resolve("Different.psd").Path.empty(),"Unrelated basename was guessed");
    WriteFile(fixture.Asset("Resolver/Models/Nested/Hidden.png"),"do not recurse");
    Check(resolve("Hidden.png").Path.empty(),"Resolver performed an unrequested recursive Assets search");
    QTemporaryDir external;Check(external.isValid(),"Cannot create outside fixture");WriteFile(external.filePath("Outside.png"),"outside");
    Check(resolve(external.filePath("Outside.png").toStdString()).Path.empty(),"Resolver accepted a texture outside Assets");
    WriteFile(fixture.Asset("Resolver/Models/Case.png"),"upper");
    QFile lower(fixture.Asset("Resolver/Models/case.PNG"));
    if(lower.open(QIODevice::WriteOnly|QIODevice::NewOnly)) {
        Check(lower.write("lower")==5,"Cannot write case variant");lower.close();
        const auto ambiguous=resolve("CASE.PNG");
        Check(ambiguous.Path.empty()&&ambiguous.Reason.find("Ambiguous")!=std::string::npos,"Same-directory case ambiguity silently bound one texture");
    } else std::cout<<"SKIP case-distinct duplicate candidates: fixture filesystem is case insensitive\n";
    std::cout<<"PASS resolver exact basename, directory priority, FBM/MTL/ancestor/relative/absolute, Windows paths, no extension guesses/outside/recursive search\n";
}
void CheckBinding(ModelFixture& fixture) {
    TestMeta red(eAssetImporterType::Texture,fixture.NewGuid(),"Valid/Paint/Red.png");
    TestMeta blue(eAssetImporterType::Texture,fixture.NewGuid(),"Valid/Paint/Blue.png");
    const auto redOutput=fixture.Texture(red),blueOutput=fixture.Texture(blue);
    WriteOBJ(fixture.Asset("Valid/Model.obj"),"newmtl Surface\nKd 1 0 0\nmap_Kd ..\\Paint\\Red.png\nnewmtl Other\nKd 0 0 1\nmap_Kd ../Paint/Blue.png\n",true);
    TestMeta model(eAssetImporterType::Model,fixture.NewGuid(),"Valid/Model.obj");
    const auto core=fixture.Core(model);CheckModel(core.Import);
    const std::set<std::string> expected{
        red._Guid.GetGuidString()+"_"+std::to_string(redOutput.Manifest.Assets[0].LocalID),
        blue._Guid.GetGuidString()+"_"+std::to_string(blueOutput.Manifest.Assets[0].LocalID)};
    Check(TextureRefs(core.Materials)==expected&&UnresolvedCount(core.Materials)==0,"Core lost normal multi-material texture bindings");
    const auto result=fixture.Import(model);CheckModel(result);
    Check(TextureRefs(fixture.Materials(result))==expected,"Pipeline changed normal texture Guid/LocalID binding");
    std::set<std::string> dependencies;
    for(const auto& dependency:result.Manifest.Dependencies) dependencies.insert(dependency.Path);
    Check(dependencies==std::set<std::string>{"Valid/Materials/Model.mtl","Valid/Paint/Red.png","Valid/Paint/Blue.png"},"Model MTL/texture dependency tracking changed");
    bool twoMeshParts=false;
    for(const auto& artifact:result.Manifest.Assets) if(artifact.Type==eAssetType::Mesh) {
        Json::Value layout;Check(AssetArtifactIO::Parse(artifact.DescriptorJson,layout),"Cannot decode mesh descriptor");
        twoMeshParts|=layout["SubMeshes"].size()==2;
    }
    Check(twoMeshParts,"Two-material mesh lost material submeshes");
    const auto cached=fixture.Import(model);Check(cached.Success&&cached.Cached,"Normal model cache was not reused");
    // A found file is not a missing file: malformed Meta must remain a hard failure.
    const auto validMeta=ReadFile(fixture.Asset(red.Path)+".meta");
    for(const std::string kind:{"json","guid","type","localid"}) {
        Json::Value invalid;
        Check(AssetArtifactIO::Parse(validMeta.toStdString(),invalid),"Cannot parse fixture Meta");
        QByteArray text;
        if(kind=="json")text="{broken json";
        else {
            if(kind=="guid")invalid["Guid"]="invalid-guid";
            if(kind=="type")invalid["ImporterType"]="Audio";
            if(kind=="localid")invalid["SubAssets"][0]["LocalID"]=Json::UInt64(0);
            text=QByteArray::fromStdString(AssetArtifactIO::JsonText(invalid));
        }
        WriteFile(fixture.Asset(red.Path)+".meta",text);
        const auto badCore=fixture.Core(model);Check(!badCore.Import.Success&&!badCore.Import.Error.empty(),"Bad texture Meta became nonfatal: "+kind);
        const auto bad=fixture.Import(model,true);Check(!bad.Success&&!bad.Error.empty(),"Pipeline swallowed bad texture Meta: "+kind);
        AssetImportManifest retained;
        Check(fixture.Library->GetManifest(model._Guid,retained)&&retained.Generation==result.Manifest.Generation,"Failed import overwrote last good model");
    }
    WriteFile(fixture.Asset(red.Path)+".meta",validMeta);
    TestMeta brokenTexture(eAssetImporterType::Texture,fixture.NewGuid(),"Bad/Image.png");
    WriteFile(fixture.Asset(brokenTexture.Path),"not png content");
    const auto broken=fixture.Import(brokenTexture);
    Check(!broken.Success&&!broken.Error.empty(),"Texture importer accepted bad image bytes");
    std::cout<<"PASS core+async Pipeline normal/multi-material binding, exact Guid+LocalID, dependencies, strict malformed Meta and Texture decode failure\n";
}
void CheckMissingCache(ModelFixture& fixture) {
    WriteOBJ(fixture.Asset("Late/Model.obj"),"newmtl Surface\nKd 0.4 0.7 0.2\nmap_Kd C:\\author\\Textures\\Late.png\n");
    TestMeta model(eAssetImporterType::Model,fixture.NewGuid(),"Late/Model.obj");
    const auto core=fixture.Core(model);CheckMissing(core.Import,core.Materials);
    const auto missing=fixture.Import(model);CheckMissing(missing,fixture.Materials(missing));
    Check(fixture.Library->FindDependents("Late/Textures/LATE.PNG")==std::vector<std::string>{model._Guid.GetGuidString()},"Missing texture basename was not reverse-indexed");
    Check(fixture.Library->FindDependents("Late/Textures/Late.psd").empty(),"Reverse texture index ignored the extension");
    const auto cachedMissing=fixture.Import(model);Check(cachedMissing.Success&&cachedMissing.Cached,"Unchanged missing reference did not cache");
    Check(cachedMissing.Diagnostics==missing.Diagnostics,"Cache hit discarded nonfatal warnings");
    TestMeta texture(eAssetImporterType::Texture,fixture.NewGuid(),"Late/Textures/Late.png");
    const auto textureOutput=fixture.Texture(texture);
    const auto found=fixture.Import(model);CheckModel(found);
    Check(!found.Cached&&found.Manifest.Generation!=missing.Manifest.Generation,"Adding an exact missing texture did not invalidate model cache");
    Check(found.Diagnostics.empty()&&UnresolvedCount(fixture.Materials(found))==0,"Resolved reference retained missing warning");
    const std::set<std::string> expected{texture._Guid.GetGuidString()+"_"+std::to_string(textureOutput.Manifest.Assets[0].LocalID)};
    Check(TextureRefs(fixture.Materials(found))==expected,"Previously missing texture did not bind after creation");
    Check(found.Manifest.SubAssetsJson==missing.Manifest.SubAssetsJson,"Resolving an external texture changed model subresource LocalIDs");
    const auto cachedFound=fixture.Import(model);Check(cachedFound.Success&&cachedFound.Cached,"Resolved model cache was not reusable");
    Check(QFile::remove(fixture.Asset(texture.Path))&&QFile::remove(fixture.Asset(texture.Path)+".meta"),"Cannot remove texture fixture");
    const auto removed=fixture.Import(model);CheckMissing(removed,fixture.Materials(removed));
    Check(!removed.Cached,"Removing resolved texture did not invalidate model cache");
    // A same-stem replacement with another extension is deliberately not a candidate.
    WriteOBJ(fixture.Asset("NoGuess/Model.obj"),"newmtl Surface\nmap_Kd Tree.psd\n");
    TestMeta png(eAssetImporterType::Texture,fixture.NewGuid(),"NoGuess/Tree.png");fixture.Texture(png);
    TestMeta noGuess(eAssetImporterType::Model,fixture.NewGuid(),"NoGuess/Model.obj");
    const auto noGuessOutput=fixture.Import(noGuess);CheckMissing(noGuessOutput,fixture.Materials(noGuessOutput));
    WriteFile(fixture.Asset("Bad/Model.fbx"),"invalid fbx bytes");
    TestMeta invalidModel(eAssetImporterType::Model,fixture.NewGuid(),"Bad/Model.fbx");
    const auto invalid=fixture.Import(invalidModel);Check(!invalid.Success&&!invalid.Error.empty(),"Malformed model became missing-texture warning");
    WriteOBJ(fixture.Asset("Directory/Model.obj"),"newmtl Surface\nmap_Kd Pretend.png\n");
    Check(QDir().mkpath(fixture.Asset("Directory/Pretend.png")),"Cannot create directory-as-texture fixture");
    TestMeta directoryAsFile(eAssetImporterType::Model,fixture.NewGuid(),"Directory/Model.obj");
    const auto directoryFailure=fixture.Import(directoryAsFile);
    Check(!directoryFailure.Success&&!directoryFailure.Error.empty(),"Directory masquerading as found texture was treated as a missing warning");
    std::cout<<"PASS missing texture warning+artifacts, warning cache persistence, exact texture creation/deletion cache invalidation, no PSD-to-PNG guessing\n";
}
void CheckCompletionRace(ModelFixture& fixture) {
    TestMeta seed(eAssetImporterType::Texture,fixture.NewGuid(),"RaceSeed/Arrived.png");
    const auto texture=fixture.Texture(seed);
    WriteOBJ(fixture.Asset("Race/Model.obj"),"newmtl Surface\nmap_Kd Arrived.png\n");
    TestMeta model(eAssetImporterType::Model,fixture.NewGuid(),"Race/Model.obj");
    fixture.Foundation.Tasks.BeforeCompletion=[&] {
        WriteFile(fixture.Asset("Race/Textures/Arrived.png"),ReadFile(fixture.Asset(seed.Path)));
        TestMeta moved(eAssetImporterType::Texture,seed._Guid,"Race/Textures/Arrived.png");moved.Identities=seed.Identities;
        WriteMeta(fixture.Asset(moved.Path),moved);
    };
    const auto obsolete=fixture.Import(model);
    Check(!obsolete.Success&&obsolete.Superseded&&obsolete.Retry,"Completion published stale missing-texture model after texture arrived");
    AssetImportManifest manifest;
    Check(!fixture.Library->GetManifest(model._Guid,manifest),"Superseded first import published a manifest");
    const auto current=fixture.Import(model);CheckModel(current);
    Check(TextureRefs(fixture.Materials(current))==std::set<std::string>{seed._Guid.GetGuidString()+"_"+std::to_string(texture.Manifest.Assets[0].LocalID)},"Retry did not resolve texture created at completion boundary");
    std::cout<<"PASS main-thread completion rejects worker result when missing texture appears before publish\n";
}
void CheckRealModels(ModelFixture& fixture,const QString& sourceAssets,bool required) {
    for(const QString name:{"SM_Env_Tree_Pine_Cluster_01.fbx","SM_Env_Tree_Pine_Cluster_04.fbx"}) {
        const QString source=QDir(sourceAssets).filePath(name);
        if(!QFileInfo::exists(source)) {
            Check(!required,"Requested real FBX fixture missing: "+source.toStdString());
            std::cout<<"SKIP optional real FBX: "<<source.toStdString()<<"\n";continue;
        }
        const auto original=ReadFile(source);
        const std::string relative="Real/"+name.toStdString();
        Check(QDir().mkpath(QFileInfo(fixture.Asset(relative)).absolutePath())&&QFile::copy(source,fixture.Asset(relative)),"Cannot copy real FBX fixture");
        Check(ReadFile(fixture.Asset(relative))==original,"FBX temporary copy differs");
        TestMeta model(eAssetImporterType::Model,fixture.NewGuid(),relative);
        const auto core=fixture.Core(model);CheckMissing(core.Import,core.Materials);
        const auto imported=fixture.Import(model);CheckMissing(imported,fixture.Materials(imported));
        Check(ReadFile(source)==original,"Original user FBX changed during isolated test");
        std::cout<<"PASS untouched real FBX "<<name.toStdString()<<" core+Pipeline with missing author texture\n";
    }
}
}
int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    Foundation::FoundationApi* real=nullptr;
    try {
        QString realAssets=QDir::home().filePath("Documents/GameSproutTest/Assets");bool required=false;
        for(int index=1;index<argc;++index) if(std::string(argv[index])=="--real-assets"&&index+1<argc) {realAssets=QString::fromLocal8Bit(argv[++index]);required=true;}
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Cannot launch real Foundation");
        {
            ModelFixture fixture(real);
            CheckResolver(fixture);CheckBinding(fixture);CheckMissingCache(fixture);CheckCompletionRace(fixture);CheckRealModels(fixture,realAssets,required);
            Check(fixture.IDE.Count==0&&fixture.IDE.Loads==fixture.IDE.Unloads,"Final Loading imbalance");
        }
        real->Release();
        std::cout<<"All model texture import checks passed.\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL "<<error.what()<<"\n";if(real)real->Release();return 1;
    }
}

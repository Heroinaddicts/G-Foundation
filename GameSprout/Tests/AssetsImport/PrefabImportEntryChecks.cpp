#include "AssetTestSupport.h"
#include <QElapsedTimer>
#include <algorithm>

namespace {
class SourceRegistry final : public IAssetsManager {
public:
    std::vector<IAssetMeta*> Sources;
    IAssetMeta* QueryAsset(const Guid& guid) const override {
        for(auto* source:Sources)if(source->_Guid==guid)return source;
        return nullptr;
    }
    std::vector<IAssetMeta*> GetAssetMetas() const override {return Sources;}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override {return Sources;}
    bool ReimportAsset(const Guid&) override {return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override {return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override {return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override {return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override {return false;}
    bool RegisterAssetListener(IAssetListener*) override {return true;}
    void UnregisterAssetListener(IAssetListener*) override {}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};
void Write(const QString& path,const QByteArray& bytes) {
    std::string error;Check(AssetArtifactIO::Write(path,bytes,error),"Cannot write fixture: "+error);
}
void WriteMeta(const QDir& assets,const TestMeta& meta) {
    std::string json;Check(meta.SerializeMeta(json),"Cannot serialize fixture Meta");
    Write(assets.filePath(QString::fromStdString(meta.Path)+".meta"),QByteArray::fromStdString(json));
}
Json::Value Reference(const TestMeta& meta,uint64_t localID) {
    Json::Value result;result["Guid"]=meta._Guid.GetGuidString();result["LocalID"]=Json::UInt64(localID);return result;
}
Json::Value Source(const TestMeta& mesh,const TestMeta& script,bool internalTarget) {
    Json::Value result;result["Version"]=1;result["Format"]="GameSproutPrefab";result["Base"]=Json::Value();result["NestedInstances"]=Json::Value(Json::arrayValue);
    auto& data=result["Data"];data["Version"]=1;data["Name"]="Mesh";data["NextObjectID"]=Json::UInt64(7);data["Objects"]=Json::Value(Json::arrayValue);
    Json::Value root;root["ID"]=Json::UInt64(1);root["ParentID"]=Json::UInt64(0);root["Name"]="Mesh";root["Components"]=Json::Value(Json::arrayValue);
    Json::Value transform;transform["ID"]=Json::UInt64(2);transform["Type"]="GameSproutRuntime.Transform";root["Components"].append(transform);
    Json::Value renderer;renderer["ID"]=Json::UInt64(3);renderer["Type"]="GameSproutRuntime.Mesh";renderer["Mesh"]=Reference(mesh,41);root["Components"].append(renderer);
    Json::Value behaviour;behaviour["ID"]=Json::UInt64(4);behaviour["InstanceID"]=Json::UInt64(2);behaviour["Type"]="GameSproutRuntime.ScriptComponent";
    behaviour["ScriptTypeID"]=script._Guid.GetGuidString();behaviour["Source"]=Reference(script,1);behaviour["Enabled"]=true;
    behaviour["Fields"]["Test::_Target"]["Kind"]="SceneObject";
    if(internalTarget)behaviour["Fields"]["Test::_Target"]["Value"]["ObjectFileID"]=Json::UInt64(5);
    else behaviour["Fields"]["Test::_Target"]["Value"]=Json::Value();
    root["Components"].append(behaviour);data["Objects"].append(root);
    Json::Value child;child["ID"]=Json::UInt64(5);child["ParentID"]=Json::UInt64(1);child["Name"]="Child";child["Components"]=Json::Value(Json::arrayValue);
    transform["ID"]=Json::UInt64(6);child["Components"].append(transform);data["Objects"].append(child);return result;
}
}

int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);QTemporaryDir temporary;
    Foundation::FoundationApi* real=nullptr;GameSproutEditor::GameSproutModuleApi* api=nullptr;
    TestIDE ide;TestProject project;TestConsole console;TestEditor editor;SourceRegistry registry;FoundationProxy foundation;
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    const auto cleanup=[&] {
        for(const std::string name:{"AssetsPipeline","ShaderCompiler","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=editor.Modules.find(name);
            if(found!=editor.Modules.end()&&std::find(initialized.begin(),initialized.end(),found->second)!=initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second,api);
        }
        initialized.clear();delete api;api=nullptr;if(real){real->Release();real=nullptr;}
    };
    try {
        Check(temporary.isValid(),"Cannot create isolated Prefab project");QDir root(temporary.path());Check(root.mkpath("Assets"),"Cannot create Assets");QDir assets(root.filePath("Assets"));
        TestMeta mesh(eAssetImporterType::Mesh,Guid(101,102,103,104),"Shape.mesh");
        TestMeta script(eAssetImporterType::Script,Guid(201,202,203,204),"Test.hpp");
        TestMeta prefab(eAssetImporterType::Prefab,Guid(301,302,303,304),"Mesh.prefab");
        registry.Sources={&mesh,&script,&prefab};Write(assets.filePath("Shape.mesh"),"prepared-mesh-source-snapshot");Write(assets.filePath("Test.hpp"),"// project script source\n");
        for(const auto* meta:{&mesh,&script,&prefab})WriteMeta(assets,*meta);
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        project.Root=temporary.path().toStdString();editor.IDE=&ide;editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;editor.Modules["AssetsManager"]=&registry;
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;api=new GameSproutEditor::GameSproutModuleApi(&foundation,&editor);
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","GuidCreator"})editor.Modules[name]=LoadModule(name);
        for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,api);
        for(const auto& entry:editor.Modules){Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,api),"Initialize "+entry.first);initialized.push_back(entry.second);}
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","GuidCreator"})Check(GameSproutEditor::GameSproutEditorFinal::Launch(editor.Modules[name],api),"Launch "+name);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project,api),"Project launch");
        auto* pipeline=static_cast<IAssetsPipeline*>(editor.Modules["AssetsPipeline"]);auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        const auto import=[&](TestMeta& input,const Json::Value& document) {
            Write(assets.filePath(QString::fromStdString(input.Path)),QByteArray::fromStdString(AssetArtifactIO::JsonText(document)));
            bool done=false;AssetImportResult result;const auto submitted=foundation.Tasks.Submissions;
            // Enter the same public module API as the editor. Do not manufacture
            // SourcePaths: AssetsPipeline must obtain it from the registry itself.
            pipeline->ImportAsset(&input,[&](const AssetImportResult& value){Check(std::this_thread::get_id()==ide.Main,"Prefab callback left main thread");result=value;done=true;},true);
            QElapsedTimer timer;timer.start();while(!done&&timer.elapsed()<10000){real->Update();QCoreApplication::processEvents();QThread::msleep(2);}
            Check(done&&foundation.Tasks.Submissions==submitted+1,"Prefab import did not complete through TaskApi");
            Check(ide.Count==0&&ide.Loads==ide.Unloads,"Prefab import did not balance Loading");
            if(result.Success)input.Identities=result.Manifest.SubAssetsJson;return result;
        };
        std::string generation;
        for(bool internalTarget:{true,false}) {
            const auto result=import(prefab,Source(mesh,script,internalTarget));Check(result.Success,"Public Prefab import failed: "+result.Error);
            Check(result.Manifest.Assets.size()==1&&result.Manifest.Assets[0].Type==eAssetType::Prefab,"Prefab main artifact missing");
            bool meshDependency=false,scriptDependency=false;
            for(const auto& dependency:result.Manifest.Dependencies){meshDependency=meshDependency||dependency.Path==mesh.Path;scriptDependency=scriptDependency||dependency.Path==script.Path;}
            Check(meshDependency&&scriptDependency,"Public Prefab import omitted mesh/script dependencies");
            auto value=ReadArtifact(library->QueryAsset(prefab._Guid,result.Manifest.Assets[0].LocalID));
            const auto& target=value["Data"]["Objects"][0]["Components"][2]["Fields"]["Test::_Target"]["Value"];
            Check(internalTarget?target["ObjectFileID"].asUInt64()==5:target.isNull(),"Prefab artifact changed internal/null object reference");generation=result.Manifest.Generation;
        }
        auto missing=Source(mesh,script,false);const Guid absent(901,902,903,904);missing["Data"]["Objects"][0]["Components"][1]["Mesh"]["Guid"]=absent.GetGuidString();
        const auto rejected=import(prefab,missing);Check(!rejected.Success&&rejected.Error.find("Referenced asset GUID is missing: "+absent.GetGuidString())!=std::string::npos,"Real missing resource was not rejected precisely");
        AssetImportManifest retained;Check(library->GetManifest(prefab._Guid,retained)&&retained.Generation==generation,"Failed Prefab replaced last published artifact");
        // Optional read-only reproduction with source snapshots from a user
        // project. All copies and produced data stay inside this temporary root.
        if(argc>1) {
            const QDir provided(QString::fromUtf8(argv[1]));
            const auto read=[&](const QString& path) {QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read provided source: "+path.toStdString());return file.readAll();};
            const auto readMeta=[&](const char* name) {Json::Value value;Check(AssetArtifactIO::Parse(read(provided.filePath(QString::fromUtf8(name)+".meta")).toStdString(),value),"Cannot parse provided Meta");return value;};
            const auto meshMeta=readMeta("WomanAnimations.fbx"),scriptMeta=readMeta("Test.hpp");
            TestMeta providedMesh(eAssetImporterType::Model,Guid(meshMeta["Guid"].asString()),"WomanAnimations.fbx");
            TestMeta providedScript(eAssetImporterType::Script,Guid(scriptMeta["Guid"].asString()),"Test.hpp");
            providedMesh.Settings=meshMeta["ImportSettings"];providedMesh.Identities=AssetArtifactIO::JsonText(meshMeta["SubAssets"]);
            providedScript.Settings=scriptMeta["ImportSettings"];providedScript.Identities=AssetArtifactIO::JsonText(scriptMeta["SubAssets"]);
            for(const char* name:{"WomanAnimations.fbx","WomanAnimations.fbx.meta","Test.hpp","Test.hpp.meta"})Write(assets.filePath(name),read(provided.filePath(name)));
            std::string removalError;Check(library->RemoveAsset(prefab._Guid,removalError),"Cannot clear isolated synthetic fixture: "+removalError);
            for(const char* name:{"Mesh.prefab","Mesh 1.prefab","Mesh 2.prefab"}) {
                const auto metadata=readMeta(name);Json::Value document;Check(AssetArtifactIO::Parse(read(provided.filePath(name)).toStdString(),document),"Cannot parse provided Prefab");
                TestMeta providedPrefab(eAssetImporterType::Prefab,Guid(metadata["Guid"].asString()),name);
                providedPrefab.Settings=metadata["ImportSettings"];providedPrefab.Identities=AssetArtifactIO::JsonText(metadata["SubAssets"]);
                WriteMeta(assets,providedPrefab);registry.Sources={&providedMesh,&providedScript,&providedPrefab};
                const auto result=import(providedPrefab,document);Check(result.Success,std::string("Provided Prefab public import failed: ")+name+": "+result.Error);
                std::cout<<"PASS copied user source through public ImportAsset: "<<name<<'\n';
            }
            registry.Sources.clear();
        }
        cleanup();std::cout<<"PASS public Prefab ImportAsset discovers mesh/script dependencies, internal/null Target imports, and missing GUID keeps last artifact\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';cleanup();return 1;}
}

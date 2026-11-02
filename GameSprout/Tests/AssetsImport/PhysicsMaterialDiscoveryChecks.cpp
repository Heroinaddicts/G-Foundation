#include "AssetTestSupport.h"
#include "../../Module/Shared/PhysicsMaterialDefinition.h"
#include <QElapsedTimer>
#include <algorithm>

// Exercise filename discovery through the same exported AssetsManager used by
// the editor. A direct RunAssetImport with a supplied Importer would miss this bug.
int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    QTemporaryDir temporary;
    Foundation::FoundationApi* real=nullptr;
    TestIDE ide;TestProject project;TestConsole console;TestEditor editor;
    FoundationProxy foundation;
    GameSproutEditor::GameSproutModuleApi api(&foundation,&editor);
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    const auto cleanup=[&] {
        for(const std::string name:{"AssetsManager","AssetsPipeline","ShaderCompiler","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=editor.Modules.find(name);
            if(found!=editor.Modules.end()&&std::find(initialized.begin(),initialized.end(),found->second)!=initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second,&api);
        }
        initialized.clear();
        if(real){real->Release();real=nullptr;}
    };
    try {
        Check(temporary.isValid(),"Cannot create isolated material discovery project");
        const QDir root(temporary.path());
        const QString generated=root.filePath("Generated");
        QProcess generator;
        generator.start(TEST_PYTHON,{TEST_GENERATOR,"--output",generated});
        Check(generator.waitForFinished(30000)&&generator.exitStatus()==QProcess::NormalExit&&generator.exitCode()==0,
            "ThirdPerson generator failed: "+generator.readAllStandardError().toStdString());
        const QDir generatedAssets(QDir(generated).filePath("Assets"));
        const QStringList materials=generatedAssets.entryList({"*.physicmaterial"},QDir::Files);
        Check(materials.size()==1&&materials.front()=="Character.physicmaterial",
            "Generated character material must use the AssetsManager-supported .physicmaterial extension");
        Check(root.mkpath("Project/Assets"),"Cannot create isolated Assets");
        project.Root=root.filePath("Project").toStdString();
        const QDir assets(root.filePath("Project/Assets"));
        for(const QString& name:{materials.front(),materials.front()+".meta"})
            Check(QFile::copy(generatedAssets.filePath(name),assets.filePath(name)),"Cannot copy generated character material");

        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;
        editor.IDE=&ide;editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator"})
            editor.Modules[name]=LoadModule(name);
        for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,&api);
        for(const auto& entry:editor.Modules) {
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,&api),"Initialize "+entry.first);
            initialized.push_back(entry.second);
        }
        for(const auto& entry:editor.Modules)if(entry.first!="Project")
            Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,&api),"Launch "+entry.first);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project,&api),"Project launch failed");
        auto* manager=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        const auto wait=[&](const std::string& path) {
            QElapsedTimer timer;timer.start();
            while(timer.elapsed()<10000) {
                real->Update();QCoreApplication::processEvents();
                for(IAssetMeta* meta:manager->GetAssetMetas())if(meta->GetPath()==path) {
                    Check(meta->GetImportState()!=eAssetImportState::Failed,"Material import failed: "+meta->GetImportError());
                    if(meta->GetImportState()==eAssetImportState::Ready&&ide.Count==0)return meta;
                }
                QThread::msleep(2);
            }
            throw std::runtime_error("AssetsManager did not discover/import material: "+path);
        };
        const auto verify=[&](IAssetMeta* meta) {
            Check(meta->_ImporterType==eAssetImporterType::PhysicsMaterial,"Source extension selected the wrong importer");
            AssetImportManifest manifest;
            Check(library->GetManifest(meta->_Guid,manifest)&&manifest.Assets.size()==1
                &&manifest.Assets.front().SourceKey=="main","Discovered material did not publish one main identity");
            IAsset* asset=library->QueryAsset(meta->_Guid,manifest.Assets.front().LocalID);
            Check(asset&&asset->GetAssetType()==eAssetType::PhysicsMaterial,"Main material is unavailable in Library");
            Check(QFileInfo(QString::fromStdString(asset->GetArtifactPath())).absolutePath()
                ==QDir(QString::fromStdString(library->GetLibraryRoot())).filePath("Artifact"),"Material was not published under Library/Artifact");
            QByteArray payload;const auto descriptor=ReadArtifact(asset,&payload);
            Check(payload.size()==16&&descriptor["Format"]=="PhysicsMaterial1","Discovered material has no prepared binary surface");
            GameSproutRuntime::PhysicsMaterialData surface;std::string error;
            Check(PhysicsMaterialDefinition::Decode(reinterpret_cast<const std::uint8_t*>(payload.constData()),
                static_cast<size_t>(payload.size()),surface,error),error);
            Check(surface.Friction==0&&surface.Restitution==0
                &&surface.FrictionCombine==GameSproutRuntime::PhysicsMaterialCombine::Minimum,"Prepared character surface lost its zero-friction Minimum combine");
        };
        GameSproutEditor::GameSproutEditorFinal::Finished(manager,&api);
        IAssetMeta* startup=wait(materials.front().toStdString());verify(startup);
        Check(startup->_Guid.GetGuidString()=="1349119f706a432ea716f963a18c2092"
            &&library->QueryAsset(startup->_Guid,UINT64_C(16618772712020502922)),"Example GUID/LocalID changed during real startup discovery");
        Check(foundation.Tasks.Submissions==1,"Startup did not submit exactly one discovered material");

        // A new source has no sidecar and must pass through the live watcher,
        // factory selection and automatic Meta creation without a forced type.
        Check(QFile::copy(generatedAssets.filePath(materials.front()),assets.filePath("Live.physicmaterial")),"Cannot create live material");
        IAssetMeta* live=wait("Live.physicmaterial");verify(live);
        Check(live->_Guid!=startup->_Guid&&QFileInfo::exists(assets.filePath("Live.physicmaterial.meta")),"Live material did not get its own Meta identity");
        Check(ide.Loads==ide.Unloads&&ide.Count==0&&console.Errors.empty(),"Discovery left errors or unbalanced Loading");
        cleanup();
        std::cout<<"PASS generated .physicmaterial through real AssetsManager startup and live Watch: stable main identity, Library/Artifact and zero friction\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';cleanup();return 1;}
}

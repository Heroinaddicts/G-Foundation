#include "../AssetsImport/AssetTestSupport.h"
#include "../../Module/RuntimeAssets/LibraryRuntimeResources.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimeLightingApi.h"
#include "RuntimeRenderApi.h"
#include <array>

namespace {
using namespace GameSproutRuntime;
ResourceReference FindResource(ILibraryManager& library,IAssetsManager& assets,const std::string& path,eAssetType type) {
    for(auto* meta:assets.GetAssetMetas())if(meta->GetPath()==path) {
        AssetImportManifest manifest;
        Check(library.GetManifest(meta->_Guid,manifest),"Missing manifest "+path+": "+meta->GetImportError());
        for(const auto& resource:manifest.Assets)if(resource.Type==type)return {manifest.GuidString,resource.LocalID};
    }
    throw std::runtime_error("Prepared resource missing: "+path);
}
Json::Value Vector(float x,float y,float z,float w) {
    Json::Value value(Json::arrayValue);for(float component:{x,y,z,w})value.append(component);return value;
}
std::array<int,3> Pixel(IRuntimeViewport& viewport) {
    std::vector<std::uint8_t> bytes;std::string error;Check(viewport.ReadPixels(bytes,error),error);
    const auto& statistics=viewport.GetStatistics();
    const std::size_t offset=(std::size_t(statistics.Height/2)*statistics.Width+statistics.Width/2)*4;
    Check(offset+4<=bytes.size(),"GPU readback extent mismatch");return {bytes[offset],bytes[offset+1],bytes[offset+2]};
}
}
int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    Foundation::FoundationApi* real=nullptr;
    TestProject project;TestConsole console;TestIDE ide;TestEditor editor;FoundationProxy foundation;
    GameSproutEditor::GameSproutModuleApi* api=nullptr;
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;
    LibraryRuntimeResources* resources=nullptr;
    const auto cleanup=[&] {
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;delete resources;resources=nullptr;
        for(const std::string name:{"AssetsManager","AssetsPipeline","ShaderCompiler","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=editor.Modules.find(name);
            if(found!=editor.Modules.end()&&std::find(initialized.begin(),initialized.end(),found->second)!=initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second,api);
        }
        initialized.clear();delete api;api=nullptr;if(real){real->Release();real=nullptr;}
    };
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create isolated Lit project");
        QDir root(temporary.path());Check(root.mkpath("Assets"),"Cannot create Assets");
        std::string error;
        Json::Value document;document["Version"]=1;document["Name"]="Lit integration";
        document["Shader"]="GameSprout/Lit";document["BaseColor"]=Vector(.5f,.25f,.125f,1);
        document["Metallic"]=0;document["Roughness"]=1;document["Emission"]=Vector(0,0,0,1);
        const auto write=[&](const QString& path,const QByteArray& bytes) {
            Check(AssetArtifactIO::Write(root.filePath("Assets/"+path),bytes,error),error);
        };
        write("Surface.mat",QByteArray::fromStdString(AssetArtifactIO::JsonText(document)));
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;
        project.Root=temporary.path().toStdString();editor.IDE=&ide;editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        api=new GameSproutEditor::GameSproutModuleApi(&foundation,&editor);
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator"})editor.Modules[name]=LoadModule(name);
        for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,api);
        for(const auto& entry:editor.Modules) {
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,api),"Initialize "+entry.first);initialized.push_back(entry.second);
        }
        for(const auto& entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,api),"Launch "+entry.first);
        auto* assets=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        const auto idle=[&] {if(ide.Count)return false;for(auto* meta:assets->GetAssetMetas())if(meta->GetImportState()==eAssetImportState::Pending)return false;return true;};
        const auto pump=[&](const std::function<bool()>& done) {
            const auto start=std::chrono::steady_clock::now();
            do {real->Update();QCoreApplication::processEvents();GameSproutEditor::GameSproutEditorFinal::Update(assets,api);
                if(done())return;QThread::msleep(2);
            }while(std::chrono::steady_clock::now()-start<std::chrono::seconds(45));
            throw std::runtime_error("Lit import timed out");
        };
        GameSproutEditor::GameSproutEditorFinal::Finished(assets,api);pump(idle);
        const auto reference=FindResource(*library,*assets,"Surface.mat",eAssetType::Material);
        resources=new LibraryRuntimeResources(assets,library);MaterialRenderData data;
        Check(resources->LoadMaterial(reference,data,error),error);
        Check(data.ShadingModel==MaterialShadingModel::Lit&&data.BaseColor.R==.5f&&data.Roughness==1,"Lit prepared fields were lost or color decoded twice");
        runtime=Export(TEST_BINARY_ROOT);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(96,96,resources,error);Check(viewport,error);
        auto* scene=runtime->CreateScene("Lit integration");auto* cube=scene->CreateGameObject("Cube");
        cube->AddComponent<IMeshComponent>()->SetMesh(GetBuiltinMeshReference(PrimitiveType::Cube));
        Check(cube->AddComponent<IMeshRendererComponent>()->SetMaterials({reference}),"Cannot set Lit material");
        RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        Check(TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Camera matrix failed");
        const auto draw=[&] {Check(viewport->Render(view),viewport->GetLastError());return Pixel(*viewport);};
        auto pixel=draw();Check(pixel[0]<3&&pixel[1]<3&&pixel[2]<3,"Lit without light/emission must be black");
        auto* lightObject=scene->CreateGameObject("Directional Light");auto* light=lightObject->AddComponent<ILightComponent>();Check(light,"Missing Light factory");
        auto settings=light->GetSettings();settings.Intensity=1;Check(light->SetSettings(settings),"Light settings failed");
        const auto lit=draw();Check(lit[0]>25&&lit[0]>lit[1]&&lit[1]>lit[2],"Prepared Lit material did not respond to scene light");
        settings.Enabled=false;Check(light->SetSettings(settings),"Disable light failed");pixel=draw();Check(pixel[0]<3,"Disabled light still illuminated");
        settings.Enabled=true;settings.Intensity=.25f;Check(light->SetSettings(settings),"Intensity update failed");pixel=draw();
        Check(pixel[0]>0&&pixel[0]<lit[0]/2,"Light intensity did not update on next Render");
        settings.Intensity=1;settings.CullingMask=1u<<3;Check(light->SetSettings(settings),"Mask update failed");pixel=draw();Check(pixel[0]<3,"Light ignored object layer mask");
        Check(cube->SetLayer(3),"Object layer failed");pixel=draw();Check(pixel[0]>25,"Object layer did not restore illumination");
        settings.Enabled=false;Check(light->SetSettings(settings),"Disable failed");
        const auto save=[&] {
            std::string previous;Check(assets->LoadMaterialSource(Guid(reference.Guid),previous,error),error);
            Check(assets->SaveMaterialSource(Guid(reference.Guid),AssetArtifactIO::JsonText(document),previous,error),error);pump(idle);
        };
        document["Emission"]=Vector(.2f,.4f,.6f,1);save();pixel=draw();
        Check(std::abs(pixel[0]-51)<=3&&std::abs(pixel[1]-102)<=3&&std::abs(pixel[2]-153)<=3,"Saved linear emission did not reach GPU");
        auto* gameView=runtime->CreateOffscreenRenderer(80,80,resources,error);Check(gameView,error);
        Check(gameView->Render(view),gameView->GetLastError());const auto reopened=Pixel(*gameView);delete gameView;
        Check(std::abs(reopened[1]-pixel[1])<3,"Second viewport did not use the same scene/material state");
        std::string previous;Check(assets->LoadMaterialSource(Guid(reference.Guid),previous,error),error);
        Json::Value bad=document;bad["Metallic"]=2;
        Check(!assets->SaveMaterialSource(Guid(reference.Guid),AssetArtifactIO::JsonText(bad),previous,error),"Out-of-range Lit property was saved");
        std::string unchanged;Check(assets->LoadMaterialSource(Guid(reference.Guid),unchanged,error)&&unchanged==previous,"Rejected material edit changed the file");
        Check(ide.Loads==ide.Unloads&&ide.Count==0,"Lit imports left Loading unbalanced");
        if(application.arguments().contains("--keep-project")) {
            // Optional manual Editor review uses the same imported data as the GPU checks.
            document["Emission"]=Vector(0,0,0,1);document["Roughness"]=.4;save();
            temporary.setAutoRemove(false);
            std::cout<<"PROJECT_ROOT="<<temporary.path().toStdString()<<'\n';
        }
        std::cout<<"PASS .mat import -> Artifact -> Runtime Lit GPU, linear color/emission, live light/intensity/layers, second viewport, atomic invalid-save rejection\n";
        cleanup();return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';cleanup();return 1;}
}

#include "AssetTestSupport.h"
#include "../../View/SceneViewUI/EditorRuntimeResources.h"
#include "GameSproutRuntimeExport.h"
#include "GRHIExport.h"

static GameSproutRuntime::ResourceReference FindResource(ILibraryManager* library, IAssetsManager* manager,
    const std::string& path, eAssetType type) {
    for (IAssetMeta* meta : manager->GetAssetMetas()) if (meta->GetPath() == path) {
        AssetImportManifest manifest;
        Check(library->GetManifest(meta->_Guid,manifest),"Missing import manifest: " + path);
        for (const AssetArtifact& artifact : manifest.Assets) if (artifact.Type == type)
            return {manifest.GuidString,artifact.LocalID};
    }
    throw std::runtime_error("Missing prepared resource: " + path);
}

int main(int argc, char** argv) {
    QGuiApplication application(argc,argv);
    Foundation::FoundationApi* real = nullptr;
    GRHI::GRHIApi* grhi = nullptr;
    GRHI::IDevice* device = nullptr;
    GRHI::ITexture* target = nullptr;
    GameSproutRuntime::IGameSproutRuntime* runtime = nullptr;
    GameSproutRuntime::IRuntimeRenderer* renderer = nullptr;
    EditorRuntimeResources* resources = nullptr;
    TestProject project; TestConsole console; TestIDE ide; TestEditor editor;
    FoundationProxy foundation;
    GameSproutEditor::GameSproutModuleApi* api = nullptr;
    bool modulesReady = false;
    const auto cleanup = [&] {
        delete renderer;renderer=nullptr;
        delete resources;resources=nullptr;
        delete target;target=nullptr;
        delete runtime;runtime=nullptr;
        delete device;device=nullptr;
        delete grhi;grhi=nullptr;
        if (modulesReady) for (const std::string& name : {"AssetsManager","AssetsPipeline","ShaderCompiler","LibraryManager","LocalDatabase","GuidCreator"})
            GameSproutEditor::GameSproutEditorFinal::Release(editor.Modules[name],api);
        modulesReady=false;
        delete api;api=nullptr;
        if(real){real->Release();real=nullptr;}
    };
    try {
        QTemporaryDir temporary;
        Check(temporary.isValid(),"Cannot create prepared rendering project");
        QDir root(temporary.path());Check(root.mkpath("Assets"),"Cannot create Assets");
        QImage image(8,8,QImage::Format_RGBA8888);image.fill(QColor(32,230,52));
        Check(image.save(root.filePath("Assets/Color.png")),"Cannot create source texture");
        std::string error;
        Check(AssetArtifactIO::Write(root.filePath("Assets/Triangle.obj"),
            "o Triangle\nv -1 -1 0\nv 1 -1 0\nv 0 1 0\nvt 0 1\nvt 1 1\nvt 0.5 0\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n",error),error);
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;
        project.Root=temporary.path().toStdString();editor.IDE=&ide;
        editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        api=new GameSproutEditor::GameSproutModuleApi(&foundation,&editor);
        for(const std::string& name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator"}) editor.Modules[name]=LoadModule(name);
        for(const auto& entry:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,api);
        for(const auto& entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,api),"Module initialize failed: "+entry.first);
        modulesReady=true;
        for(const auto& entry:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,api),"Module launch failed");
        auto* manager=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        const auto pump = [&](const std::function<bool()>& done) {
            const auto start=std::chrono::steady_clock::now();
            do {
                real->Update();QCoreApplication::processEvents();
                GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
                if(done())return;
                QThread::msleep(2);
            }while(std::chrono::steady_clock::now()-start<std::chrono::seconds(30));
            throw std::runtime_error("Prepared rendering import timed out");
        };
        const auto idle = [&] {
            if(ide.Count)return false;
            for(IAssetMeta* meta:manager->GetAssetMetas())if(meta->GetImportState()==eAssetImportState::Pending)return false;
            return true;
        };
        GameSproutEditor::GameSproutEditorFinal::Finished(manager,api);
        pump([&]{return manager->GetAssetMetas().size()==2&&idle();});
        const auto texture=FindResource(library,manager,"Color.png",eAssetType::Texture);
        const auto mesh=FindResource(library,manager,"Triangle.obj",eAssetType::Mesh);
        Json::Value material;material["Version"]=1;material["DoubleSided"]=true;
        material["BaseColorTexture"]["Guid"]=texture.Guid;material["BaseColorTexture"]["LocalID"]=Json::UInt64(texture.LocalID);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Green.mat"),QByteArray::fromStdString(AssetArtifactIO::JsonText(material)),error),error);
        pump([&]{return manager->GetAssetMetas().size()==3&&idle();});
        const auto materialReference=FindResource(library,manager,"Green.mat",eAssetType::Material);
        resources=new EditorRuntimeResources(manager,library);
        GameSproutRuntime::MeshRenderData meshData;
        GameSproutRuntime::TextureRenderData textureData;
        GameSproutRuntime::MaterialRenderData materialData;
        Check(resources->LoadMesh(mesh,meshData,error),error);
        Check(meshData.VertexCount==3&&meshData.IndexCount==3&&meshData.UVOffset>=0,"Prepared mesh layout lost");
        Check(resources->LoadTexture(texture,textureData,error),error);
        Check(textureData.Width==8&&textureData.Height==8&&textureData.Pixels[1]==230,"Texture mip0 data lost");
        Check(resources->LoadMaterial(materialReference,materialData,error)&&materialData.BaseColorTexture==texture,error);
        Check(!resources->LoadMesh(texture,meshData,error)&&meshData.Bytes.empty(),"Wrong resource type accepted");
        IAsset* artifact=library->QueryAsset(Guid(mesh.Guid),mesh.LocalID);
        const QString artifactPath=QString::fromStdString(artifact->GetArtifactPath());
        QFile file(artifactPath);Check(file.open(QIODevice::ReadOnly),"Cannot read artifact");const QByteArray original=file.readAll();file.close();
        Check(AssetArtifactIO::Write(artifactPath,original.left(original.size()-1),error),error);
        Check(!resources->LoadMesh(mesh,meshData,error)&&meshData.Bytes.empty(),"Truncated artifact accepted");
        Check(AssetArtifactIO::Write(artifactPath,original,error),error);
        if(application.arguments().contains("--provider-only")) {
            std::cout<<"PASS prepared mesh/texture/material layouts, typed references and corrupt artifact rejection\n";
            cleanup();return 0;
        }
        // 原始 source 不参与绘制：上传 Library 内的顶点/索引/像素，验证真实 GPU 像素。
        grhi=ExportGRHIApi(TEST_BINARY_ROOT);Check(grhi,"Cannot load GRHI");
        device=grhi->CreateDevice();Check(device,"Cannot create GPU device");
        runtime=Export(TEST_BINARY_ROOT);Check(runtime,"Cannot load Runtime");
        renderer=runtime->CreateRenderer(device,resources);Check(renderer,"Cannot create Runtime renderer");
        auto* scene=runtime->CreateScene("Prepared Scene");
        auto* object=scene->CreateGameObject("Imported Triangle");
        object->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(mesh);
        object->AddComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({materialReference});
        GRHI::TextureDesc targetDesc;targetDesc.Width=192;targetDesc.Height=192;targetDesc.Usage=GRHI::TextureUsage::ColorTarget;
        target=device->CreateTexture(targetDesc);Check(target,device->GetLastError());
        GameSproutRuntime::RenderView view;view.Scene=scene;view.ColorTarget=target;view.DrawGrid=false;
        view.Camera.Position={0,0,3};
        Check(GameSproutRuntime::TryMakeLookAt({0,0,3},{0,0,0},{0,1,0},view.Camera.View),"Camera failed");
        Check(renderer->Render(view),renderer->GetLastError());
        Check(renderer->GetStatistics().VisibleObjects==1&&renderer->GetStatistics().Triangles==1,"Imported geometry did not draw");
        std::vector<std::uint8_t> pixels;
        Check(device->ReadTexture(target,pixels),device->GetLastError());
        const std::size_t center=(96*192+96)*4;
        Check(pixels.size()==192*192*4&&pixels[center+1]>pixels[center]*2&&pixels[center+1]>pixels[center+2]*2,"Prepared texture/material not visible in GPU output");
        QImage preview(pixels.data(),192,192,QImage::Format_RGBA8888);
        preview.save("/tmp/gamesprout-prepared-render.png");
        const std::uint64_t revision=resources->GetRevision();
        Check(manager->ReimportAsset(Guid(texture.Guid)),"Cannot reimport texture");
        pump([&]{return idle()&&resources->GetRevision()>revision;});
        Check(renderer->Render(view),"Renderer did not recover from import revision: "+renderer->GetLastError());
        std::cout<<"PASS imported OBJ/PNG/material -> ArtifactDB provider -> Runtime -> GRHI GPU pixels, corrupt artifact rejection, reimport cache refresh\n";
        cleanup();return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";cleanup();return 1;}
}

#include "../AssetsImport/AssetTestSupport.h"
#include "IRuntimeAssets.h"
#include "IProjectSettings.h"
#include "SceneDocument.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeLightingApi.h"
#include "RuntimeRenderApi.h"
#include <QFile>
#include <QElapsedTimer>
#include <QBuffer>
#include <QImage>
#include <QColorSpace>
#include <array>

namespace {
using namespace GameSproutRuntime;
class BridgeEditor final:public GameSproutEditor::GameSproutViewApi {
public:
    explicit BridgeEditor(IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    TestIDE* IDE=nullptr;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{return name=="IDEWindow"?IDE:nullptr;}
};
Json::Value Array(std::initializer_list<float> numbers) {
    Json::Value result(Json::arrayValue);for(float value:numbers)result.append(value);return result;
}
Json::Value Ref(const char* guid) {Json::Value value;value["Guid"]=guid;value["LocalID"]=Json::UInt64(1);return value;}
void Write(const QDir& root,const QString& path,const QByteArray& bytes) {
    std::string error;Check(AssetArtifactIO::Write(root.filePath(path),bytes,error),error);
}
void WriteJson(const QDir& root,const QString& path,const Json::Value& value) {
    Write(root,path,QByteArray::fromStdString(AssetArtifactIO::JsonText(value)));
}
void Meta(const QDir& root,const QString& path,const char* guid,const char* importer,eAssetType type) {
    Json::Value value;value["Version"]=1;value["Guid"]=guid;value["Path"]=path.toStdString();value["ImporterType"]=importer;
    value["ImportSettings"]=Json::Value(Json::objectValue);Json::Value resource;
    resource["LocalID"]=Json::UInt64(1);resource["Type"]=static_cast<unsigned>(type);resource["Name"]="main";resource["SourceKey"]="main";
    value["SubAssets"]=Json::Value(Json::arrayValue);value["SubAssets"].append(resource);WriteJson(root,"Assets/"+path+".meta",value);
}
void Fixture(const QDir& root) {
    Check(root.mkpath("Assets")&&root.mkpath("Info"),"Cannot create fixture project");
    QByteArray mesh("GSMESH01",8);QDataStream output(&mesh,QIODevice::Append);
    output.setByteOrder(QDataStream::LittleEndian);output.setFloatingPointPrecision(QDataStream::SinglePrecision);
    const QByteArray name("Bridge triangle");
    output<<quint32(1)<<quint32(5)<<quint32(3)<<quint32(3)<<quint32(1)<<quint32(name.size());
    output.writeRawData(name.data(),name.size());while(mesh.size()%4)output<<quint8(0);
    for(float value:{-1.f,-1.f,0.f,1.f,-1.f,0.f,0.f,1.f,0.f})output<<value;
    for(int vertex=0;vertex<3;++vertex)output<<0.f<<0.f<<1.f;
    for(float value:{0.f,0.f,1.f,0.f,.5f,1.f})output<<value;
    output<<quint32(0)<<quint32(1)<<quint32(2)<<quint32(0)<<quint32(3)<<quint32(0);
    Write(root,"Assets/Triangle.mesh",mesh);
    constexpr const char* meshGuid="6666777788884999aabbccddeeff0011";
    constexpr const char* materialGuid="6666777788884999aabbccddeeff0022";
    Meta(root,"Triangle.mesh",meshGuid,"Mesh",eAssetType::Mesh);
    Json::Value material;material["Version"]=1;material["Name"]="Bridge Lit";material["Shader"]="GameSprout/Lit";
    material["BaseColor"]=Array({.15f,.45f,.8f,1});WriteJson(root,"Assets/Surface.mat",material);
    Meta(root,"Surface.mat",materialGuid,"Material",eAssetType::Material);
    // Unity's 2x2 EncodeToPNG probe has blue in the PNG's top row while
    // GetPixel(0,0) is red. The bridge reverses rows before GameSprout import:
    // row zero must therefore remain red when sampled with the original UVs.
    constexpr const char* textureGuid="6666777788884999aabbccddeeff0033";
    constexpr const char* probeGuid="6666777788884999aabbccddeeff0044";
    QImage image(2,2,QImage::Format_RGBA8888);image.fill(Qt::red);
    image.setPixelColor(0,1,Qt::blue);image.setPixelColor(1,1,Qt::blue);
    QByteArray png;QBuffer pngOutput(&png);Check(pngOutput.open(QIODevice::WriteOnly)&&image.save(&pngOutput,"PNG"),"Cannot encode UV probe");
    Write(root,"Assets/UnityUV.png",png);Meta(root,"UnityUV.png",textureGuid,"Texture",eAssetType::Texture);
    Json::Value probe;probe["Version"]=1;probe["Shader"]="GameSprout/Preview";
    probe["BaseColorTexture"]=Ref(textureGuid);WriteJson(root,"Assets/UnityUV.mat",probe);
    Meta(root,"UnityUV.mat",probeGuid,"Material",eAssetType::Material);
    Json::Value scene;scene["Version"]=1;scene["Name"]="Bridge fixture";scene["MainCameraID"]=Json::UInt64(5);scene["NextObjectID"]=Json::UInt64(11);
    scene["Objects"]=Json::Value(Json::arrayValue);
    Json::Value node;node["ID"]=Json::UInt64(1);node["ParentID"]=Json::UInt64(0);node["Name"]="Triangle";node["Active"]=true;
    Json::Value transform;transform["ID"]=Json::UInt64(2);transform["Type"]="GameSproutRuntime.Transform";
    transform["Position"]=Array({0,0,0});transform["Rotation"]=Array({0,0,0,1});transform["Scale"]=Array({1,1,1});
    node["Components"].append(transform);Json::Value component;component["ID"]=Json::UInt64(3);component["Type"]="GameSproutRuntime.Mesh";component["Mesh"]=Ref(meshGuid);node["Components"].append(component);
    component=Json::Value();component["ID"]=Json::UInt64(4);component["Type"]="GameSproutRuntime.MeshRenderer";component["Visible"]=true;
    component["Materials"].append(Ref(materialGuid));node["Components"].append(component);scene["Objects"].append(node);
    node=Json::Value();node["ID"]=Json::UInt64(5);node["ParentID"]=Json::UInt64(0);node["Name"]="Main Camera";node["Active"]=true;
    transform["ID"]=Json::UInt64(6);transform["Position"]=Array({0,0,3});node["Components"].append(transform);
    component=Json::Value();component["ID"]=Json::UInt64(7);component["Type"]="GameSproutRuntime.Camera";node["Components"].append(component);scene["Objects"].append(node);
    node=Json::Value();node["ID"]=Json::UInt64(8);node["ParentID"]=Json::UInt64(0);node["Name"]="Directional Light";node["Active"]=true;
    transform["ID"]=Json::UInt64(9);transform["Position"]=Array({0,0,0});node["Components"].append(transform);
    component=Json::Value();component["ID"]=Json::UInt64(10);component["Type"]="GameSproutRuntime.Light";node["Components"].append(component);scene["Objects"].append(node);
    WriteJson(root,"Assets/Bridge.scene",scene);Json::Value info;info["Project"]["name"]="Bridge fixture";WriteJson(root,"Info/project.json",info);
}
Json::Value ReadJson(const QString& path) {
    QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read "+path.toStdString());Json::Value result;
    Check(AssetArtifactIO::Parse(file.readAll().toStdString(),result),"Invalid JSON "+path.toStdString());return result;
}
void SavePreview(const std::vector<std::uint8_t>& pixels,const QString& path) {
    // Offscreen readback is linear RGBA8. PNG viewers expect sRGB, matching the
    // Editor's sRGB swapchain. This transfer is only for the exported preview;
    // GPU validation and all lighting calculations continue to use raw linear data.
    QImage linear(pixels.data(),640,360,QImage::Format_RGBA8888);
    linear.setColorSpace(QColorSpace::SRgbLinear);
    Check(linear.convertedToColorSpace(QColorSpace::SRgb).save(path),"Cannot save preview");
}
}
int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);Foundation::FoundationApi* real=nullptr;
    IGameSproutRuntime* runtime=nullptr;IRuntimeViewport* viewport=nullptr;
    TestProject project;TestConsole console;TestIDE ide;FoundationProxy foundation;
    BridgeEditor* editor=nullptr;GameSproutEditor::GameSproutModuleApi* api=nullptr;
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    const auto cleanup=[&] {
        delete viewport;viewport=nullptr;
        if(editor)for(const std::string name:{"RuntimeAssets","AssetsManager","AssetsPipeline","ShaderCompiler","ProjectSettings","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=editor->Modules.find(name);
            if(found!=editor->Modules.end()&&std::find(initialized.begin(),initialized.end(),found->second)!=initialized.end())GameSproutEditor::GameSproutEditorFinal::Release(found->second,api);
        }
        delete runtime;runtime=nullptr;delete api;api=nullptr;delete editor;editor=nullptr;if(real){real->Release();real=nullptr;}
    };
    try {
        QTemporaryDir temporary;QString root;bool fixture=application.arguments().contains("--fixture");bool renderAll=false,compareEnvironment=false,compareBatching=false;
        for(const auto& argument:application.arguments()) {
            if(argument.startsWith("--project-root="))root=argument.mid(QStringLiteral("--project-root=").size());
            if(argument=="--render-all")renderAll=true;
            if(argument=="--compare-environment")compareEnvironment=true;
            if(argument=="--compare-batching")compareBatching=true;
        }
        if(fixture){Check(temporary.isValid(),"Cannot create temporary fixture");root=temporary.path();Fixture(QDir(root));}
        Check(!root.isEmpty()&&QFileInfo::exists(root+"/Info/project.json"),"Use --fixture or --project-root=<exported project>");
        runtime=Export(TEST_BINARY_ROOT);Check(runtime,"Runtime load failed");editor=new BridgeEditor(runtime);editor->IDE=&ide;
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;project.Root=root.toStdString();
        editor->Modules["Project"]=&project;editor->Modules["Console"]=&console;api=new GameSproutEditor::GameSproutModuleApi(&foundation,editor);
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","RuntimeAssets","ProjectSettings"})editor->Modules[name]=LoadModule(name);
        for(const auto& entry:editor->Modules)GameSproutEditor::GameSproutEditorFinal::Before(entry.second,api);
        for(const auto& entry:editor->Modules){Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second,api),"Initialize "+entry.first);initialized.push_back(entry.second);}
        for(const auto& entry:editor->Modules)Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second,api),"Launch "+entry.first);
        auto* assets=static_cast<IAssetsManager*>(editor->Modules["AssetsManager"]);auto* library=static_cast<ILibraryManager*>(editor->Modules["LibraryManager"]);
        QElapsedTimer timer;timer.start();GameSproutEditor::GameSproutEditorFinal::Finished(assets,api);
        for(;;) {
            real->Update();QCoreApplication::processEvents();GameSproutEditor::GameSproutEditorFinal::Update(assets,api);
            bool pending=ide.Count!=0;for(auto* meta:assets->GetAssetMetas())pending=pending||meta->GetImportState()==eAssetImportState::Pending;
            if(!pending)break;Check(timer.elapsed()<600000,"Bridge import timed out");QThread::msleep(2);
        }
        std::cout<<"Bridge resource scan/import complete, elapsedMs="<<timer.elapsed()<<std::endl;
        auto* resources=static_cast<IRuntimeAssets*>(editor->Modules["RuntimeAssets"])->GetResourceProvider();Check(resources,"Missing Runtime resource provider");
        std::size_t meshes=0,materials=0,textures=0;std::uint64_t triangles=0;std::string scenePath,error;
        for(auto* meta:assets->GetAssetMetas()) {
            Check(meta->GetImportState()!=eAssetImportState::Failed,"Import failed "+meta->GetPath()+": "+meta->GetImportError());
            AssetImportManifest manifest;if(!library->GetManifest(meta->_Guid,manifest))continue;
            for(const auto& asset:manifest.Assets) {
                const ResourceReference ref{meta->_Guid.GetGuidString(),asset.LocalID};
                if(asset.Type==eAssetType::Mesh){MeshRenderData mesh;Check(resources->LoadMesh(ref,mesh,error),meta->GetPath()+": "+error);++meshes;triangles+=mesh.IndexCount/3;}
                else if(asset.Type==eAssetType::Material){MaterialRenderData material;Check(resources->LoadMaterial(ref,material,error),meta->GetPath()+": "+error);++materials;}
                else if(asset.Type==eAssetType::Texture){TextureRenderData texture;Check(resources->LoadTexture(ref,texture,error),meta->GetPath()+": "+error);++textures;}
                else if(asset.Type==eAssetType::Scene)scenePath=meta->GetPath();
            }
        }
        Check(!scenePath.empty(),"Export contains no scene");const auto document=ReadJson(root+"/Assets/"+QString::fromStdString(scenePath));
        IScene* scene=nullptr;SceneDocument::ObjectIDs identities;Check(SceneDocument::Instantiate(document,*runtime,scene,identities,error),error);
        scene->SetSimulationEnabled(false);std::size_t nodes=0,renderers=0,colliders=0,unboundRenderers=0,missing=0;
        std::vector<IGameObject*> pending{scene};
        while(!pending.empty()) {
            auto* object=pending.back();pending.pop_back();if(object!=scene)++nodes;
            for(std::size_t i=0;i<object->GetChildCount();++i)pending.push_back(object->GetChild(i));
            if(auto* renderer=object->GetComponent<IMeshRendererComponent>()) {
                ++renderers;auto* mesh=object->GetComponent<IMeshComponent>();
                // Unity can contain renderers whose MeshFilter has no mesh. Preserve that
                // source state, but reject any non-empty reference that cannot resolve.
                if(!mesh||mesh->GetMesh().Guid.empty())++unboundRenderers;
                else if(!library->QueryAsset(Guid(mesh->GetMesh().Guid),mesh->GetMesh().LocalID))++missing;
                for(std::size_t i=0;i<renderer->GetMaterialCount();++i){const auto ref=renderer->GetMaterial(i);if(!ref.Guid.empty()&&!library->QueryAsset(Guid(ref.Guid),ref.LocalID))++missing;}
            }
            for(std::size_t i=0;i<object->GetComponentCount();++i)if(dynamic_cast<IColliderComponent*>(object->GetComponentAt(i)))++colliders;
        }
        Check(!missing,"Scene contains missing mesh/material references");
        if(!fixture) {
            const auto report=ReadJson(root+"/Info/UnityExportManifest.json");
            const auto& counts=report["Counts"];
            Check(counts["Nodes"].asUInt64()==nodes,"Runtime node count differs from Unity export");
            Check(counts["RendererComponents"].asUInt64()==renderers,"Runtime renderer count differs from Unity export");
            Check(counts["MeshSources"].asUInt64()==meshes&&counts["MaterialSources"].asUInt64()==materials&&
                  counts["TextureSources"].asUInt64()==textures,"Imported resource counts differ from Unity export");
            const auto expectedColliders=counts.get("BoxColliderComponents",0).asUInt64()+counts.get("SphereColliderComponents",0).asUInt64()+
                counts.get("CapsuleColliderComponents",0).asUInt64()+counts.get("MeshColliderComponents",0).asUInt64();
            Check(expectedColliders==colliders,"Runtime collider count differs from Unity export");
        }
        std::cout<<"Bridge scene restored: nodes="<<nodes<<" colliders="<<colliders<<", elapsedMs="<<timer.elapsed()<<std::endl;
        if(fixture||renderAll) {
            auto* camera=scene->GetMainCamera();Check(camera,"No enabled MainCamera selected by exporter");
            RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={.08f,.1f,.15f,1};
            view.Camera.Projection=camera->GetSettings();Check(camera->GetViewMatrix(view.Camera.View),"Invalid camera view");
            const Mat4 matrix=camera->GetGameObject()->GetTransform()->GetWorldMatrix();view.Camera.Position={matrix(0,3),matrix(1,3),matrix(2,3)};
            viewport=runtime->CreateOffscreenRenderer(640,360,resources,error);Check(viewport,error);Check(viewport->Render(view),viewport->GetLastError());
            std::vector<std::uint8_t> pixels;Check(viewport->ReadPixels(pixels,error),error);
            Check(viewport->GetStatistics().DrawCalls>0,"Exported scene produced no draws");
            if(fixture){const auto offset=(std::size_t(180)*640+320)*4;Check(pixels[offset+2]>pixels[offset]+20,"Converted native triangle/material not rendered correctly");}
            if(fixture) {
                auto* renderer=scene->GetChild(0)->GetComponent<IMeshRendererComponent>();Check(renderer,"Missing UV probe renderer");
                renderer->SetMaterial(0,{"6666777788884999aabbccddeeff0044",1});
                Check(viewport->Render(view),viewport->GetLastError());Check(viewport->ReadPixels(pixels,error),error);
                const auto bottom=(std::size_t(245)*640+320)*4,top=(std::size_t(110)*640+320)*4;
                Check(pixels[bottom]>pixels[bottom+2]+80&&pixels[top+2]>pixels[top]+80,
                      "Unity PNG row conversion no longer matches Runtime UV sampling");
            }
            if(renderAll)SavePreview(pixels,root+"/BridgePreview.png");
            if(compareBatching) {
                // Both modes render the same scene/camera/materials; compare the
                // complete readback rather than inferring success from counters.
                const auto batched=viewport->GetStatistics();
                auto individual=view;individual.EnableGPUInstancing=false;
                Check(viewport->Render(individual),viewport->GetLastError());
                std::vector<std::uint8_t> baseline;Check(viewport->ReadPixels(baseline,error),error);
                const auto unbatched=viewport->GetStatistics();
                Check(batched.Triangles==unbatched.Triangles&&batched.VisibleObjects==unbatched.VisibleObjects,
                    "Automatic instancing changed visible geometry");
                Check(baseline.size()==pixels.size(),"Instancing readback extent changed");
                std::size_t different=0;double totalDifference=0;
                for(std::size_t offset=0;offset<pixels.size();offset+=4) {
                    bool changed=false;
                    for(std::size_t channel=0;channel<4;++channel) {
                        const int delta=std::abs(int(baseline[offset+channel])-int(pixels[offset+channel]));
                        totalDifference+=delta;changed|=delta>2;
                    }
                    different+=changed?1:0;
                }
                Check(different<=pixels.size()/400&&totalDifference/pixels.size()<.15,
                    "Instanced full-scene image differs materially from individual draws");
                const auto measure=[&](const RenderView& option) {
                    Check(viewport->Render(option),viewport->GetLastError());
                    QElapsedTimer frames;frames.start();
                    for(int frame=0;frame<10;++frame)Check(viewport->Render(option),viewport->GetLastError());
                    return double(frames.nsecsElapsed())/1e6/10;
                };
                const double individualMs=measure(individual),instancedMs=measure(view);
                Check(viewport->GetStatistics().InstanceBufferUploads==0,"Unchanged full scene uploaded instances again");
                std::cout<<"Batching comparison: individualDraws="<<unbatched.DrawCalls<<" instancedDraws="<<batched.DrawCalls
                    <<" savedDraws="<<batched.SavedDrawCalls<<" instances="<<batched.InstancedInstances
                    <<" triangles="<<batched.Triangles<<" changedPixels="<<different
                    <<" meanChannelDifference="<<totalDifference/pixels.size()
                    <<" residentBytes="<<viewport->GetStatistics().ResidentInstanceBytes
                    <<" warmIndividualRenderMs="<<individualMs<<" warmInstancedRenderMs="<<instancedMs<<std::endl;
                Check(viewport->ReadPixels(pixels,error),error);
            }
            if(renderAll&&compareEnvironment) {
                const auto environment=scene->GetEnvironmentLighting();
                const auto statistics=viewport->GetStatistics();
                auto disabled=environment;disabled.Mode=AmbientLightMode::None;
                Check(scene->SetEnvironmentLighting(disabled),"Cannot isolate direct-light baseline");
                std::vector<std::uint8_t> baseline;
                Check(viewport->Render(view),viewport->GetLastError());Check(viewport->ReadPixels(baseline,error),error);
                Check(viewport->GetStatistics().DrawCalls==statistics.DrawCalls&&viewport->GetStatistics().Triangles==statistics.Triangles,
                    "Environment lighting changed geometry submission");
                Check(scene->SetEnvironmentLighting(environment),"Cannot restore environment after comparison");
                SavePreview(baseline,root+"/BridgePreviewWithoutEnvironment.png");
                double before=0,after=0;
                for(std::size_t offset=0;offset<pixels.size();offset+=4)for(std::size_t channel=0;channel<3;++channel) {
                    before+=baseline[offset+channel];after+=pixels[offset+channel];
                }
                std::cout<<"Environment linear RGB sum: disabled="<<before<<" enabled="<<after<<std::endl;
            }
            std::cout<<"GPU draws="<<viewport->GetStatistics().DrawCalls<<" triangles="<<viewport->GetStatistics().Triangles<<'\n';
        }
        Check(ide.Count==0&&ide.Loads==ide.Unloads,"Unbalanced Loading/Unloading");
        std::cout<<"PASS bridge project: nodes="<<nodes<<" renderers="<<renderers<<" sourceUnboundRenderers="<<unboundRenderers<<" colliders="<<colliders<<" meshes="<<meshes<<" materials="<<materials<<" textures="<<textures<<" uniqueMeshTriangles="<<triangles<<" imports="<<foundation.Tasks.Submissions<<" elapsedMs="<<timer.elapsed()<<'\n';
        cleanup();return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';cleanup();return 1;}
}

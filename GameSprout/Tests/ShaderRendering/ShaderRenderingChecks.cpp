#include "../AssetsImport/AssetTestSupport.h"
#include "../../Module/RuntimeAssets/LibraryRuntimeResources.h"
#include "GameSproutRuntimeExport.h"
#include "RuntimeBuiltinResources.h"
#include "RuntimeRenderApi.h"
#include "IShaderAuthoring.h"
#include <array>

namespace {
const char* ShaderSource=R"hlsl(/* GameSproutShader {
 "Name":"Tests/ActualUnlit","Passes":[{"Name":"Forward","LightMode":"Forward",
 "Vertex":"VSMain","Fragment":"PSMain","Cull":"Off","Blend":"Opaque",
 "DepthTest":true,"DepthWrite":true,"DepthCompare":"LessEqual"}]
} */
/* GameSproutMaterial {
 "Name":"Tests/ActualUnlit","Properties":[
 {"Name":"Tint","DisplayName":"颜色","Type":"Color","Default":[1,0,0,1]},
 {"Name":"Gain","DisplayName":"强度","Type":"Range","Min":0,"Max":1,"Default":1},
 {"Name":"SurfaceMap","DisplayName":"贴图","Type":"Texture","Default":null}]
} */
#include "Includes/Finish.hlsli"
cbuffer GameSproutDraw : register(b0,space1) {
 column_major float4x4 Model;
 column_major float4x4 ViewProjection;
 column_major float4x4 NormalMatrix;
};
cbuffer MaterialParameters : register(b0,space3) {
 float4 Tint;
 float4 SurfaceMapST;
 float Gain;
};
Texture2D<float4> SurfaceMap : register(t0,space2);
SamplerState SurfaceSampler : register(s0,space2);
struct VertexInput {
 [[vk::location(0)]] float3 Position:TEXCOORD0;
 [[vk::location(1)]] float3 Normal:TEXCOORD1;
 [[vk::location(2)]] float4 Color:TEXCOORD2;
 [[vk::location(3)]] float2 UV:TEXCOORD3;
};
struct Varyings {float4 Position:SV_Position;float4 Color:TEXCOORD0;float2 UV:TEXCOORD1;};
Varyings VSMain(VertexInput input) {
 Varyings output;
 output.Position=mul(ViewProjection,mul(Model,float4(input.Position,1)));
 output.Color=input.Color;output.UV=input.UV;return output;
}
float4 PSMain(Varyings input):SV_Target0 {
 float4 color=SurfaceMap.Sample(SurfaceSampler,input.UV*SurfaceMapST.xy+SurfaceMapST.zw)*input.Color*Tint;
 color.rgb*=Gain;return FinishColor(color);
}
)hlsl";

GameSproutRuntime::ResourceReference FindResource(ILibraryManager& library,IAssetsManager& assets,
    const std::string& path,eAssetType type) {
    for(auto* meta:assets.GetAssetMetas())if(meta->GetPath()==path) {
        AssetImportManifest manifest;
        Check(library.GetManifest(meta->_Guid,manifest),"No manifest for "+path+": "+meta->GetImportError());
        for(const auto& asset:manifest.Assets)if(asset.Type==type)return {manifest.GuidString,asset.LocalID};
    }
    throw std::runtime_error("Prepared resource not found: "+path);
}
std::array<int,4> Pixel(GameSproutRuntime::IRuntimeViewport& viewport) {
    std::vector<std::uint8_t> bytes;std::string error;
    Check(viewport.ReadPixels(bytes,error),error);
    const auto& frame=viewport.GetStatistics();
    const std::size_t offset=(std::size_t(frame.Height/2)*frame.Width+frame.Width/2)*4;
    Check(offset+4<=bytes.size(),"Invalid GPU readback extent");
    return {bytes[offset],bytes[offset+1],bytes[offset+2],bytes[offset+3]};
}
}

int main(int argc,char** argv) {
    QGuiApplication application(argc,argv);
    Foundation::FoundationApi* real=nullptr;
    TestProject project;TestConsole console;TestIDE ide;TestEditor editor;FoundationProxy foundation;
    GameSproutEditor::GameSproutModuleApi* api=nullptr;
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    GameSproutRuntime::IRuntimeViewport* viewport=nullptr;
    LibraryRuntimeResources* resources=nullptr;
    const auto cleanup=[&] {
        delete viewport;viewport=nullptr;delete runtime;runtime=nullptr;delete resources;resources=nullptr;
        for(const std::string name:{"ShaderAuthoring","AssetsManager","AssetsPipeline","ShaderCompiler","LibraryManager","LocalDatabase","GuidCreator"}) {
            const auto found=editor.Modules.find(name);
            if(found!=editor.Modules.end()&&std::find(initialized.begin(),initialized.end(),found->second)!=initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second,api);
        }
        initialized.clear();delete api;api=nullptr;if(real){real->Release();real=nullptr;}
    };
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create isolated shader project");
        QDir root(temporary.path());Check(root.mkpath("Assets/Includes"),"Cannot create shader source root");
        std::string error;
        const auto write=[&](const QString& path,const QByteArray& bytes) {
            Check(AssetArtifactIO::Write(root.filePath("Assets/"+path),bytes,error),error);
        };
        write("Includes/Finish.hlsli","float4 FinishColor(float4 value) { return value; }\n");
        write("Surface.shader",ShaderSource);
        real=Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real&&real->Initialize(argc,argv)&&real->Launch(),"Foundation startup failed");
        foundation.Real=real;foundation.Tasks.Real=real->TaskApi();foundation.Tasks.IDE=&ide;
        project.Root=temporary.path().toStdString();editor.IDE=&ide;
        editor.Modules["Project"]=&project;editor.Modules["Console"]=&console;
        api=new GameSproutEditor::GameSproutModuleApi(&foundation,&editor);
        for(const std::string name:{"LocalDatabase","LibraryManager","ShaderCompiler","AssetsPipeline","AssetsManager","GuidCreator","ShaderAuthoring"})
            editor.Modules[name]=LoadModule(name);
        for(const auto& item:editor.Modules)GameSproutEditor::GameSproutEditorFinal::Before(item.second,api);
        for(const auto& item:editor.Modules) {
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(item.second,api),"Initialize "+item.first);
            initialized.push_back(item.second);
        }
        for(const auto& item:editor.Modules)Check(GameSproutEditor::GameSproutEditorFinal::Launch(item.second,api),"Launch "+item.first);
        auto* assets=static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        auto* library=static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        const auto idle=[&] {
            if(ide.Count)return false;
            for(auto* meta:assets->GetAssetMetas())if(meta->GetImportState()==eAssetImportState::Pending)return false;
            return true;
        };
        const auto pump=[&](const std::function<bool()>& done) {
            const auto start=std::chrono::steady_clock::now();
            do {
                real->Update();QCoreApplication::processEvents();
                GameSproutEditor::GameSproutEditorFinal::Update(assets,api);
                if(done())return;
                QThread::msleep(2);
            }while(std::chrono::steady_clock::now()-start<std::chrono::seconds(50));
            throw std::runtime_error("Shader integration import timeout");
        };
        GameSproutEditor::GameSproutEditorFinal::Finished(assets,api);pump(idle);
        const auto shader=FindResource(*library,*assets,"Surface.shader",eAssetType::Shader);
        Json::Value document;document["Version"]=1;document["Name"]="Actual user shader";
        document["Shader"]["Guid"]=shader.Guid;document["Shader"]["LocalID"]=Json::UInt64(shader.LocalID);
        write("Surface.mat",QByteArray::fromStdString(AssetArtifactIO::JsonText(document)));
        pump([&]{return assets->GetAssetMetas().size()>=4&&idle();});
        const auto material=FindResource(*library,*assets,"Surface.mat",eAssetType::Material);
        resources=new LibraryRuntimeResources(assets,library);
        GameSproutRuntime::ShaderRenderData compiled;
        Check(resources->LoadShader(shader,compiled,error),error);
        Check(compiled.Passes.size()==1&&!compiled.Passes.front().Vertex.Codes.empty()
            &&!compiled.Passes.front().Fragment.Codes.empty(),"Prepared Shader omitted linked stages");
        runtime=Export(TEST_BINARY_ROOT);Check(runtime,"Cannot load Runtime");
        viewport=runtime->CreateOffscreenRenderer(128,128,resources,error);Check(viewport,error);
        auto* scene=runtime->CreateScene("Actual custom Shader");auto* object=scene->CreateGameObject("Cube");
        object->AddComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(
            GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Cube));
        Check(object->AddComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({material}),"Cannot assign material");
        GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,0,3};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Invalid test camera");
        const auto draw=[&] {Check(viewport->Render(view),viewport->GetLastError());return Pixel(*viewport);};
        auto pixel=draw();Check(pixel[0]>240&&pixel[1]<5&&pixel[2]<5,"User HLSL did not render unlit red");
        const auto save=[&] {
            std::string previous;Check(assets->LoadMaterialSource(Guid(material.Guid),previous,error),error);
            Check(assets->SaveMaterialSource(Guid(material.Guid),AssetArtifactIO::JsonText(document),previous,error),error);
            pump(idle);
        };
        document["Tint"]=Json::Value(Json::arrayValue);for(float value:{0,1,0,1})document["Tint"].append(value);
        save();pixel=draw();Check(pixel[1]>240&&pixel[0]<5&&pixel[2]<5,"Saved arbitrary material color not bound to GPU");
        document["Gain"]=0.25;save();pixel=draw();
        Check(pixel[1]>55&&pixel[1]<75&&pixel[0]<5,"Reflected scalar offset was not bound correctly");
        document["Gain"]=1;
        QImage image(8,4,QImage::Format_RGBA8888);
        for(int y=0;y<4;++y)for(int x=0;x<8;++x)image.setPixelColor(x,y,x<4?QColor(255,0,0):QColor(0,255,0));
        Check(image.save(root.filePath("Assets/Colors.png")),"Cannot write texture fixture");
        pump([&]{return assets->GetAssetMetas().size()>=5&&idle();});
        const auto texture=FindResource(*library,*assets,"Colors.png",eAssetType::Texture);
        document["SurfaceMap"]["Guid"]=texture.Guid;document["SurfaceMap"]["LocalID"]=Json::UInt64(texture.LocalID);
        document["Tint"]=Json::Value(Json::arrayValue);for(float value:{1,1,1,1})document["Tint"].append(value);
        document["SurfaceMapST"]=Json::Value(Json::arrayValue);for(float value:{0.f,0.f,.25f,.5f})document["SurfaceMapST"].append(value);
        save();pixel=draw();Check(pixel[0]>240&&pixel[1]<5,"Named texture binding did not sample red region");
        document["SurfaceMapST"][2]=.75;save();pixel=draw();Check(pixel[1]>240&&pixel[0]<5,"Texture ST reflection did not sample green region");
        const auto revision=resources->GetRevision();
        write("Includes/Finish.hlsli","float4 FinishColor(float4 value) { return float4(0,0,1,value.a); }\n");
        Check(assets->ReimportAsset(Guid(shader.Guid)),"Cannot request shader include rebuild");
        pump([&]{return idle()&&resources->GetRevision()>revision;});
        pixel=draw();Check(pixel[2]>240&&pixel[0]<5&&pixel[1]<5,"Changed include did not replace cached GPU program");
        auto* reopened=runtime->CreateOffscreenRenderer(128,128,resources,error);Check(reopened,error);
        Check(reopened->Render(view),reopened->GetLastError());
        const auto reopenedPixel=Pixel(*reopened);delete reopened;
        Check(reopenedPixel[2]>240&&reopenedPixel[0]<5,"Fresh renderer did not load the persisted Shader programs");
        auto* authoring=static_cast<IShaderAuthoring*>(editor.Modules["ShaderAuthoring"]);
        std::string created;
        Check(authoring->CreateUnlitShader("","Generated",created,error),error);
        Check(created=="Generated.shader","Unexpected Shader creation path");
        pump([&]{return assets->GetAssetMetas().size()>=6&&idle();});
        const auto generatedShader=FindResource(*library,*assets,created,eAssetType::Shader);
        Check(authoring->CreateMaterial("","Generated",generatedShader,created,error),error);
        Check(created=="Generated.mat","Unexpected Material creation path");
        pump([&]{return assets->GetAssetMetas().size()>=7&&idle();});
        const auto generatedMaterial=FindResource(*library,*assets,created,eAssetType::Material);
        Check(object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({generatedMaterial}),"Cannot assign generated Material");
        pixel=draw();Check(pixel[0]>240&&pixel[1]>240&&pixel[2]>240,"Editor-generated Unlit template did not render its defaults");
        Check(!viewport->GetStatistics().RequiresContinuousRendering,"Static Shader incorrectly requested continuous rendering");
        // Compile the real project water example through the same asset pipeline,
        // then verify both VS and FS frame constants with actual GPU readback.
        QFile waterSource(TEST_WATER_SHADER_SOURCE);
        Check(waterSource.open(QIODevice::ReadOnly),"Cannot open example water Shader");
        write("Water.shader",waterSource.readAll());
        pump([&]{return assets->GetAssetMetas().size()>=8&&idle();});
        const auto waterShader=FindResource(*library,*assets,"Water.shader",eAssetType::Shader);
        Check(authoring->CreateMaterial("","WaterSurface",waterShader,created,error),error);
        pump([&]{return assets->GetAssetMetas().size()>=9&&idle();});
        const auto waterMaterial=FindResource(*library,*assets,"WaterSurface.mat",eAssetType::Material);
        object->GetComponent<GameSproutRuntime::IMeshComponent>()->SetMesh(
            GameSproutRuntime::GetBuiltinMeshReference(GameSproutRuntime::PrimitiveType::Plane));
        Check(object->GetComponent<GameSproutRuntime::IMeshRendererComponent>()->SetMaterials({waterMaterial}),"Cannot assign water material");
        view.Camera.Position={0,4,5};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Invalid water camera");
        Check(runtime->BeginExecution(error),error);
        runtime->AdvanceFrame(.1f,0,0);
        draw();Check(viewport->GetStatistics().RequiresContinuousRendering,"Water frame constants did not request animated rendering");
        std::vector<std::uint8_t> firstWater,sameFrameWater,nextWater;
        Check(viewport->ReadPixels(firstWater,error),error);
        auto* secondView=runtime->CreateOffscreenRenderer(128,128,resources,error);Check(secondView,error);
        const bool secondRendered=secondView->Render(view);
        const bool secondRead=secondRendered&&secondView->ReadPixels(sameFrameWater,error);
        delete secondView;
        Check(secondRendered&&secondRead,"Second water viewport failed");
        Check(firstWater==sameFrameWater,"Two views advanced or disagreed on the shared Shader frame time");
        runtime->AdvanceFrame(.8f,0,0);draw();Check(viewport->ReadPixels(nextWater,error),error);
        Check(firstWater.size()==nextWater.size(),"Water readback size changed");
        std::size_t changedPixels=0;
        for(std::size_t index=0;index+3<firstWater.size();index+=4)
            if(firstWater[index]!=nextWater[index]||firstWater[index+1]!=nextWater[index+1]||firstWater[index+2]!=nextWater[index+2])++changedPixels;
        Check(changedPixels>64,"Water Shader did not animate on the GPU when Runtime time advanced");
        Check(runtime->EndExecution(error),error);
        // Isolate foam from lighting/reflectance, so distant filtering cannot
        // turn subpixel crests into the broad bright patches seen in the city.
        std::string waterText;
        Check(assets->LoadMaterialSource(Guid(waterMaterial.Guid),waterText,error),error);
        Json::Value foamDocument;Check(AssetArtifactIO::Parse(waterText,foamDocument),"Cannot parse water material fixture");
        for(const char* name:{"DeepColor","ShallowColor","SkyColor"}) {
            foamDocument[name]=Json::Value(Json::arrayValue);
            for(float value:{0,0,0,1})foamDocument[name].append(value);
        }
        foamDocument["FoamColor"]=Json::Value(Json::arrayValue);
        for(float value:{1,1,1,1})foamDocument["FoamColor"].append(value);
        for(const char* name:{"WaveHeight","NormalStrength","FlowSpeed","FresnelStrength","SunStrength"})foamDocument[name]=0;
        foamDocument["Opacity"]=1;foamDocument["FoamAmount"]=1;
        const auto saveFoam=[&] {
            std::string previous;Check(assets->LoadMaterialSource(Guid(waterMaterial.Guid),previous,error),error);
            Check(assets->SaveMaterialSource(Guid(waterMaterial.Guid),AssetArtifactIO::JsonText(foamDocument),previous,error),error);
            pump(idle);
        };
        saveFoam();
        object->GetTransform()->SetLocalScale({4096,1,4096});
        view.Camera.Position={0,100,0};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,0,1},view.Camera.View),"Invalid foam filtering camera");
        view.Camera.Projection.Projection=GameSproutRuntime::CameraProjection::Orthographic;
        const auto foamEnergy=[&](float span) {
            view.Camera.Projection.OrthographicHeight=span;draw();
            std::vector<std::uint8_t> pixels;Check(viewport->ReadPixels(pixels,error),error);
            std::uint64_t sum=0;
            for(std::size_t index=0;index+3<pixels.size();index+=4)sum+=pixels[index]+pixels[index+1]+pixels[index+2];
            return double(sum)/(pixels.size()/4*3);
        };
        const double nearFoam=foamEnergy(64),farFoam=foamEnergy(2048);
        Check(nearFoam>0,"Foam isolation fixture did not render visible crests");
        Check(farFoam<nearFoam,"Subpixel foam expanded into a brighter distant surface");
        foamDocument["FoamAmount"]=0;saveFoam();
        Check(foamEnergy(64)==0,"FoamAmount=0 left bright blocks in an otherwise black water material");
        std::cout<<"Water isolated foam mean RGB: near="<<nearFoam<<", far="<<farFoam<<'\n';
        Check(!resources->LoadShader(texture,compiled,error)&&compiled.Passes.empty()&&!error.empty(),"Wrong resource type accepted as Shader");
        Check(ide.Count==0&&ide.Loads==ide.Unloads,"Shader import left unbalanced Loading/Unloading");
        std::cout<<"PASS actual .shader VS/FS compile -> Artifact -> Material -> Runtime/GRHI GPU; material parameters, include rebuild, water animation, shared frame constants and distant foam filtering\n";
        cleanup();return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL "<<failure.what()<<'\n';cleanup();return 1;}
}

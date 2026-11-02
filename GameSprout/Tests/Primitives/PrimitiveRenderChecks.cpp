#include "GameSproutRuntimeExport.h"
#include "RuntimeRenderApi.h"
#include "RuntimeBuiltinResources.h"
#include <iostream>
#include <stdexcept>
class EmptyProvider final:public GameSproutRuntime::IRuntimeResourceProvider {
public:
    int MeshReads=0;
    std::uint64_t GetRevision()const override{return 1;}
    bool LoadMesh(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MeshRenderData&,std::string& error)override{++MeshReads;error="No project mesh files";return false;}
    bool LoadTexture(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::TextureRenderData&,std::string&)override{return false;}
    bool LoadMaterial(const GameSproutRuntime::ResourceReference&,GameSproutRuntime::MaterialRenderData&,std::string&)override{return false;}
};
static void Check(bool value,const std::string& error){if(!value)throw std::runtime_error(error);}
int main(int argc,char** argv){
    GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;GameSproutRuntime::IRuntimeViewport* viewport=nullptr;EmptyProvider provider;
    try{
        Check(argc==2,"Pass current Runtime directory");runtime=Export(argv[1]);Check(runtime,"Runtime unavailable");std::string error;
        viewport=runtime->CreateOffscreenRenderer(128,128,&provider,error);
        if(!viewport){delete runtime;std::cout<<"SKIP GPU unavailable: "<<error<<'\n';return 77;}
        auto* scene=runtime->CreateScene("Primitive render");auto* object=scene->CreateGameObject("Primitive");
        auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();Check(object->AddComponent<GameSproutRuntime::IMeshRendererComponent>(),"No renderer");
        GameSproutRuntime::RenderView view;view.Scene=scene;view.DrawGrid=false;view.ClearColor={0,0,0,1};view.Camera.Position={0,3,4};
        Check(GameSproutRuntime::TryMakeLookAt(view.Camera.Position,{0,0,0},{0,1,0},view.Camera.View),"Invalid camera");
        for(unsigned id=1;id<=6;++id){
            mesh->SetMesh({GameSproutRuntime::BuiltinMeshGuid,id});Check(viewport->Render(view),viewport->GetLastError());
            Check(viewport->GetStatistics().VisibleObjects==1&&viewport->GetStatistics().Triangles>0&&viewport->GetStatistics().SkippedObjects==0,viewport->GetLastError());
            std::vector<std::uint8_t> pixels;Check(viewport->ReadPixels(pixels,error),error);bool colored=false;
            for(std::size_t index=0;index+3<pixels.size();index+=4)if(pixels[index]||pixels[index+1]||pixels[index+2]){colored=true;break;}
            Check(colored,"Built-in mesh produced no visible pixels");
        }
        Check(provider.MeshReads==0,"Built-in geometry incorrectly requested project artifacts");
        mesh->SetMesh({GameSproutRuntime::BuiltinMeshGuid,99});Check(viewport->Render(view),viewport->GetLastError());
        Check(viewport->GetStatistics().SkippedObjects==1&&provider.MeshReads==0,"Unknown reserved ID fell through to project resources");
        delete viewport;delete runtime;std::cout<<"PASS six built-in meshes submit real GPU draws and readback without project resources\n";return 0;
    }catch(const std::exception& error){delete viewport;delete runtime;std::cerr<<error.what()<<'\n';return 1;}
}

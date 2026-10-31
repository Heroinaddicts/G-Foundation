#include "GRHIExport.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

static void Check(bool value,const std::string& error) { if(!value)throw std::runtime_error(error); }
struct Fixture {
    GRHI::GRHIApi* Api=nullptr;
    GRHI::IDevice* Device=nullptr;
    GRHI::IDevice* Other=nullptr;
    std::vector<GRHI::IResource*> Resources;
    ~Fixture() {for(auto it=Resources.rbegin();it!=Resources.rend();++it)delete *it;delete Other;delete Device;delete Api;}
    template<typename T>T* Keep(T* value,const std::string& name) {
        Check(value!=nullptr,name+": "+Device->GetLastError());Resources.push_back(value);return value;
    }
    void Destroy(GRHI::IResource* value) {std::erase(Resources,value);delete value;}
};
struct Recording {
    GRHI::IDevice* Device;
    GRHI::ICommandBuffer* Command;
    GRHI::IRenderPass* Pass=nullptr;
    explicit Recording(GRHI::IDevice* device):Device(device),Command(device->AcquireCommandBuffer()) {Check(Command!=nullptr,device->GetLastError());}
    ~Recording() {if(Command){if(Pass)Command->EndRenderPass(Pass);Device->Cancel(Command);}}
    bool Submit() {if(Pass){Command->EndRenderPass(Pass);Pass=nullptr;}auto* value=Command;Command=nullptr;return Device->Submit(value);}
};

#if defined(__APPLE__)
static const char Shader[] = R"(
#include <metal_stdlib>
using namespace metal;
struct Input { float3 position [[attribute(0)]]; float3 color [[attribute(1)]]; };
struct Output { float4 position [[position]]; float3 color; };
struct Camera { float4x4 matrix; };
struct Tint { float4 color; };
vertex Output vertexMain(Input input [[stage_in]],constant Camera& camera [[buffer(0)]]) {
    Output output;output.position=camera.matrix*float4(input.position,1);output.color=input.color;return output;
}
fragment float4 fragmentMain(Output input [[stage_in]],constant Tint& tint [[buffer(0)]]) {
    return float4(input.color,1)*tint.color;
}
)";
struct Vertex {float Position[3];float Color[3];};
struct Geometry {Vertex Vertices[6];uint32_t Indices[6];};
static Geometry MeshData() {
    return {{{{-.6f,-.6f,.2f},{0,1,0}},{{.6f,-.6f,.2f},{0,1,0}},{{0,.6f,.2f},{0,1,0}},
             {{-.6f,-.6f,.8f},{1,0,0}},{{.6f,-.6f,.8f},{1,0,0}},{{0,.6f,.8f},{1,0,0}}},{0,1,2,3,4,5}};
}
static GRHI::ITexture* Color(Fixture& f,uint32_t width=64) {
    GRHI::TextureDesc desc;desc.Width=width;desc.Height=64;desc.Usage=GRHI::TextureUsage::ColorTarget;
    return f.Keep(f.Device->CreateTexture(desc),"color target");
}
static GRHI::ITexture* Depth(Fixture& f,uint32_t width=64) {
    GRHI::TextureDesc desc;desc.Width=width;desc.Height=64;desc.Format=GRHI::TextureFormat::D32_Float;desc.Usage=GRHI::TextureUsage::DepthStencilTarget;
    return f.Keep(f.Device->CreateTexture(desc),"depth target");
}
static GRHI::IBuffer* Buffer(Fixture& f,const Geometry& geometry,GRHI::IDevice* device=nullptr) {
    GRHI::BufferDesc desc;desc.Size=sizeof(geometry);desc.Usage=GRHI::BufferUsage::Vertex|GRHI::BufferUsage::Index;
    GRHI::BufferInitialData data{&geometry,sizeof(geometry)};
    return f.Keep((device?device:f.Device)->CreateBuffer(desc,&data),"combined buffer");
}
static GRHI::IGraphicsPipeline* Pipeline(Fixture& f,bool useDepth) {
    GRHI::ShaderCode code;code.Format=GRHI::ShaderCodeFormat::MSL;code.Data=Shader;code.DataSize=sizeof(Shader);code.EntryPoint="vertexMain";code.UniformBufferCount=1;
    GRHI::ShaderDesc shader;shader.Codes=&code;shader.CodeCount=1;
    auto* vertex=f.Keep(f.Device->CreateShader(shader),"vertex shader");
    shader.Stage=GRHI::ShaderStage::Fragment;code.EntryPoint="fragmentMain";
    auto* fragment=f.Keep(f.Device->CreateShader(shader),"fragment shader");
    const GRHI::VertexBindingDesc binding{0,sizeof(Vertex)};
    const GRHI::VertexAttributeDesc attributes[]={{0,0,GRHI::VertexFormat::Float3,0},{1,0,GRHI::VertexFormat::Float3,12}};
    GRHI::GraphicsPipelineDesc desc;desc.VertexShader=vertex;desc.FragmentShader=fragment;desc.ColorFormat=GRHI::TextureFormat::RGBA8_UNorm;
    desc.VertexBindings=&binding;desc.VertexBindingCount=1;desc.VertexAttributes=attributes;desc.VertexAttributeCount=2;
    desc.DepthFormat=GRHI::TextureFormat::D32_Float;desc.DepthTest=useDepth;desc.DepthWrite=useDepth;desc.DepthCompare=GRHI::CompareOp::Less;
    auto* pipeline=f.Keep(f.Device->CreateGraphicsPipeline(desc),"3D pipeline");
    // The pipeline owns native shader state independently from these wrappers.
    f.Destroy(fragment);f.Destroy(vertex);return pipeline;
}
static GRHI::RenderPassDesc PassDesc(GRHI::ITexture* color,GRHI::ITexture* depth) {
    GRHI::RenderPassDesc desc;desc.ColorTarget=color->GetColorTargetView();desc.DepthTarget=depth->GetDepthTargetView();desc.ClearColor={.05f,.1f,.9f,1};return desc;
}
static void Bind(GRHI::IRenderPass* pass,GRHI::IGraphicsPipeline* pipeline,GRHI::IBuffer* buffer) {
    pass->SetPipeline(pipeline);pass->SetVertexBuffer(0,buffer);pass->SetIndexBuffer(buffer,GRHI::IndexFormat::UInt32,offsetof(Geometry,Indices));
    std::array<float,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,.3f,0,0,1};
    std::array<float,4> tint{1,.5f,1,1};
    pass->SetVertexUniform(0,matrix.data(),sizeof(matrix));pass->SetFragmentUniform(0,tint.data(),sizeof(tint));
    matrix.fill(0);tint.fill(0); // Uniform data must already be copied.
}
static std::array<uint8_t,4> Pixel(const std::vector<uint8_t>& bytes,int x,int y) {
    const size_t offset=static_cast<size_t>(y*64+x)*4;return {bytes[offset],bytes[offset+1],bytes[offset+2],bytes[offset+3]};
}
static void GPUChecks(Fixture& f) {
    auto* color=Color(f);auto* depth=Depth(f);auto geometry=MeshData();auto* buffer=Buffer(f,geometry);
    auto* pipeline=Pipeline(f,true);auto* noDepth=Pipeline(f,false);
    Check(buffer->GetSize()==sizeof(Geometry) && buffer->GetUsage()==(GRHI::BufferUsage::Vertex|GRHI::BufferUsage::Index),"Buffer metadata mismatch");
    Check(!depth->GetColorTargetView().Token && depth->GetDepthTargetView().Token,"Depth view domain mismatch");
    std::memset(&geometry,0,sizeof(geometry)); // Upload must not borrow the initial bytes.
    {
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Check(r.Pass,"Cannot begin 3D pass");
        Bind(r.Pass,pipeline,buffer);r.Pass->DrawIndexed(6);Check(r.Submit(),f.Device->GetLastError());
    }
    std::vector<uint8_t> bytes;Check(f.Device->ReadTexture(color,bytes),f.Device->GetLastError());
    Check(bytes.size()==64*64*4,"Wrong readback byte span");
    const auto green=Pixel(bytes,41,32),background=Pixel(bytes,19,44);
    Check(green[0]<10 && green[1]>115 && green[1]<140 && green[2]<10 && green[3]>245,"Indexed triangle, fragment uniform or depth occlusion failed");
    Check(background[2]>210 && background[1]<40,"Vertex matrix translation or vertex input failed");
    {
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,pipeline,buffer);r.Pass->Draw(3);
        Check(r.Submit(),f.Device->GetLastError());
        Check(f.Device->ReadTexture(color,bytes),f.Device->GetLastError());Check(Pixel(bytes,41,32)==green,"Non-indexed vertex-buffer draw failed");
    }
    {
        const auto mesh=MeshData();const uint16_t indices[]={0,1,2,3,4,5};
        std::vector<uint8_t> source(sizeof(mesh.Vertices)+sizeof(indices));
        std::memcpy(source.data(),mesh.Vertices,sizeof(mesh.Vertices));std::memcpy(source.data()+sizeof(mesh.Vertices),indices,sizeof(indices));
        const GRHI::BufferDesc desc{source.size(),GRHI::BufferUsage::Vertex|GRHI::BufferUsage::Index};
        const GRHI::BufferInitialData initial{source.data(),source.size()};
        auto* small=f.Keep(f.Device->CreateBuffer(desc,&initial),"UInt16 index buffer");
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,pipeline,small);
        r.Pass->SetIndexBuffer(small,GRHI::IndexFormat::UInt16,sizeof(mesh.Vertices));r.Pass->DrawIndexed(3,1,3,-3);
        Check(r.Submit(),f.Device->GetLastError());Check(f.Device->ReadTexture(color,bytes),f.Device->GetLastError());
        Check(Pixel(bytes,41,32)==green,"UInt16, firstIndex or valid negative vertexOffset failed");
    }
    {
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,noDepth,buffer);r.Pass->DrawIndexed(6);
        Check(r.Submit(),f.Device->GetLastError());
    }
    Check(f.Device->ReadTexture(color,bytes),f.Device->GetLastError());const auto red=Pixel(bytes,41,32);
    Check(red[0]>240 && red[1]<10,"Depth-disabled pipeline did not draw far triangle last");
    const auto rejected=[&](const std::function<void(Recording&)>& operation) {
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Check(r.Pass,"Invalid baseline pass");
        Bind(r.Pass,pipeline,buffer);operation(r);Check(!r.Submit(),"Invalid recording reached GPU submission");Check(!f.Device->GetLastError().empty(),"Missing validation diagnostic");
    };
    rejected([](Recording& r){r.Pass->DrawIndexed(1000);});
    rejected([](Recording& r){r.Pass->DrawIndexed(3,1,0,-1);});
    rejected([](Recording& r){std::array<float,4> value{};r.Pass->SetVertexUniform(1,value.data(),sizeof(value));});
    rejected([](Recording& r){std::array<float,3> value{};r.Pass->SetFragmentUniform(0,value.data(),sizeof(value));});
    rejected([](Recording& r){r.Pass->SetVertexUniform(0,nullptr,64);});
    rejected([](Recording& r){r.Pass->SetVertexBuffer(0,nullptr);});
    auto* released=Buffer(f,MeshData());released->Release();
    rejected([&](Recording& r){r.Pass->SetVertexBuffer(0,released);});
    auto* deleted=Buffer(f,MeshData());f.Destroy(deleted);
    rejected([&](Recording& r){r.Pass->SetIndexBuffer(deleted);});
    auto* late=Buffer(f,MeshData());
    rejected([&](Recording& r){r.Pass->SetVertexBuffer(0,late);r.Pass->DrawIndexed(3);late->Release();});
    auto wrong=MeshData();wrong.Indices[0]=999999;auto* outOfRange=Buffer(f,wrong);
    rejected([&](Recording& r){r.Pass->SetIndexBuffer(outOfRange,GRHI::IndexFormat::UInt32,offsetof(Geometry,Indices));r.Pass->DrawIndexed(3);});
    rejected([](Recording& r){auto* ended=r.Pass;r.Command->EndRenderPass(r.Pass);r.Pass=nullptr;ended->Draw(3);});
    {
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));r.Pass->SetPipeline(pipeline);r.Pass->SetVertexBuffer(0,buffer);r.Pass->Draw(3);
        Check(!r.Submit(),"Draw accepted missing uniform bindings");
    }
    {
        auto* wrongDepth=Depth(f,32);Recording r(f.Device);Check(!r.Command->BeginRenderPass(PassDesc(color,wrongDepth)),"Mismatched depth dimensions accepted");Check(!r.Submit(),"Failed BeginRenderPass was submitted");
    }
    {
        auto* doomed=Color(f);Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(doomed,depth));Bind(r.Pass,pipeline,buffer);r.Pass->DrawIndexed(3);doomed->Release();Check(!r.Submit(),"Released target was submitted");
    }
    f.Other=f.Api->CreateDevice();Check(f.Other,"Cannot create second physical-device context");
    auto* foreign=Buffer(f,MeshData(),f.Other);
    rejected([&](Recording& r){r.Pass->SetVertexBuffer(0,foreign);});
    {
        Recording r(f.Device);Check(!f.Other->Submit(r.Command),"Foreign command consumed by another device");
        Check(f.Device->GetLastError().empty(),"Foreign-device failure contaminated owner diagnostic");
    }
    bytes={1};Check(!f.Other->ReadTexture(color,bytes) && bytes.empty(),"Foreign readback accepted or retained output");
    Check(!f.Device->ReadTexture(depth,bytes) && bytes.empty(),"Depth screenshot treated as RGBA");
    GRHI::BufferDesc invalid;Check(!f.Device->CreateBuffer(invalid),"Zero-size buffer accepted");
    invalid.Size=16;GRHI::BufferInitialData bad{nullptr,8};Check(!f.Device->CreateBuffer(invalid,&bad),"Null upload span accepted");
    Check(f.Device->ReadTexture(color,bytes),f.Device->GetLastError());Check(Pixel(bytes,41,32)==red,"Failed recording modified target pixels");
    std::cout<<"PASS real Metal: combined vertex/index upload, matrix/fragment uniforms, depth occlusion, RGBA readback, invalid ownership/state/ranges/lifetimes\n";
}

static void ImageCompatibility(Fixture& f) {
    static const char source[]=R"(
#include <metal_stdlib>
using namespace metal;
struct Output {float4 position [[position]];float2 uv;};
vertex Output imageVertex(uint id [[vertex_id]]) {
    float2 p[3]={float2(-1,1),float2(3,1),float2(-1,-3)};
    float2 uv[3]={float2(0,0),float2(2,0),float2(0,2)};
    Output out;out.position=float4(p[id],0,1);out.uv=uv[id];return out;
}
fragment float4 imageFragment(Output in [[stage_in]],texture2d<float> image [[texture(0)]],sampler imageSampler [[sampler(0)]]) {return image.sample(imageSampler,in.uv);}
)";
    GRHI::ShaderCode code;code.Format=GRHI::ShaderCodeFormat::MSL;code.Data=source;code.DataSize=sizeof(source);code.EntryPoint="imageVertex";
    GRHI::ShaderDesc desc;desc.Codes=&code;desc.CodeCount=1;
    auto* vertex=f.Keep(f.Device->CreateShader(desc),"procedural vertex shader");
    desc.Stage=GRHI::ShaderStage::Fragment;code.EntryPoint="imageFragment";code.SamplerCount=1;
    auto* fragment=f.Keep(f.Device->CreateShader(desc),"image fragment shader");
    GRHI::GraphicsPipelineDesc pipelineDesc;pipelineDesc.VertexShader=vertex;pipelineDesc.FragmentShader=fragment;pipelineDesc.ColorFormat=GRHI::TextureFormat::RGBA8_UNorm;
    auto* pipeline=f.Keep(f.Device->CreateGraphicsPipeline(pipelineDesc),"legacy no-buffer pipeline");
    const uint8_t orange[]={255,64,0,255};
    GRHI::TextureDesc imageDesc;imageDesc.Width=1;imageDesc.Height=1;
    const GRHI::TextureInitialData data{orange,sizeof(orange),0};
    auto* image=f.Keep(f.Device->CreateTexture(imageDesc,&data),"sampled image");
    auto* sampler=f.Keep(f.Device->CreateSampler({}),"image sampler");auto* color=Color(f);
    Recording r(f.Device);GRHI::RenderPassDesc pass;pass.ColorTarget=color->GetColorTargetView();r.Pass=r.Command->BeginRenderPass(pass);
    Check(r.Pass,"Cannot begin image compatibility pass");r.Pass->SetPipeline(pipeline);r.Pass->SetFragmentTextureSampler(0,image,sampler);r.Pass->Draw(3);
    Check(r.Submit(),f.Device->GetLastError());std::vector<uint8_t> pixels;Check(f.Device->ReadTexture(color,pixels),f.Device->GetLastError());
    Check(Pixel(pixels,32,32)==std::array<uint8_t,4>{255,64,0,255},"Legacy texture/sampler/procedural draw regressed");
    imageDesc.Format=GRHI::TextureFormat::BGRA8_UNorm;const uint8_t bgra[]={10,20,30,255};const GRHI::TextureInitialData bgraData{bgra,4,0};
    auto* bgraImage=f.Keep(f.Device->CreateTexture(imageDesc,&bgraData),"BGRA texture");
    Check(f.Device->ReadTexture(bgraImage,pixels) && pixels==std::vector<uint8_t>({30,20,10,255}),"BGRA readback did not normalize to RGBA");
    std::cout<<"PASS compatible procedural image draw, texture/sampler bindings and BGRA-to-RGBA readback\n";
}
#endif
int main(int argc,char** argv) {
#if !defined(__APPLE__)
    (void)argc;(void)argv;std::cout<<"SKIP real GPU fixture currently supplies Metal source; API remains platform independent\n";return 77;
#else
    try {Check(argc==2,"Expected runtime binary directory");Fixture fixture;fixture.Api=ExportGRHIApi(argv[1]);Check(fixture.Api,"Cannot load GRHI");fixture.Device=fixture.Api->CreateDevice();Check(fixture.Device,"Cannot create Metal device");GPUChecks(fixture);ImageCompatibility(fixture);return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
#endif
}

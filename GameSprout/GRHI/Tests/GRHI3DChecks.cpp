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
struct InstanceInput {float3 position [[attribute(0)]];float3 color [[attribute(1)]];float2 offset [[attribute(2)]];};
struct ResidentInput {float3 position [[attribute(0)]];float3 color [[attribute(1)]];uint index [[attribute(2)]];};
struct Output { float4 position [[position]]; float3 color; };
struct Camera { float4x4 matrix; };
struct Tint { float4 color; };
vertex Output vertexMain(Input input [[stage_in]],constant Camera& camera [[buffer(0)]]) {
    Output output;output.position=camera.matrix*float4(input.position,1);output.color=input.color;return output;
}
vertex Output instanceMain(InstanceInput input [[stage_in]],constant Camera& camera [[buffer(0)]]) {
    Output output;output.position=camera.matrix*float4(input.position.xy*.3+input.offset,input.position.z,1);
    output.color=input.color;return output;
}
vertex Output residentMain(ResidentInput input [[stage_in]],constant Camera& camera [[buffer(0)]],
                           const device float2* offsets [[buffer(1)]]) {
    Output output;output.position=camera.matrix*float4(input.position.xy*.3+offsets[input.index],input.position.z,1);
    output.color=input.color;return output;
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
enum class TestInstanceData { None, Vertex, Resident };
static GRHI::IGraphicsPipeline* Pipeline(Fixture& f,bool useDepth,TestInstanceData data=TestInstanceData::None) {
    const bool instanced=data!=TestInstanceData::None;
    const bool resident=data==TestInstanceData::Resident;
    GRHI::ShaderCode code;code.Format=GRHI::ShaderCodeFormat::MSL;code.Data=Shader;code.DataSize=sizeof(Shader);
    code.EntryPoint=resident?"residentMain":instanced?"instanceMain":"vertexMain";
    code.UniformBufferCount=1;code.VertexStorageBufferCount=resident?1:0;
    GRHI::ShaderDesc shader;shader.Codes=&code;shader.CodeCount=1;
    auto* vertex=f.Keep(f.Device->CreateShader(shader),"vertex shader");
    shader.Stage=GRHI::ShaderStage::Fragment;code.EntryPoint="fragmentMain";code.VertexStorageBufferCount=0;
    auto* fragment=f.Keep(f.Device->CreateShader(shader),"fragment shader");
    const GRHI::VertexBindingDesc bindings[]={{0,sizeof(Vertex)},{1,resident?4u:8u,GRHI::VertexInputRate::Instance}};
    const GRHI::VertexAttributeDesc attributes[]={{0,0,GRHI::VertexFormat::Float3,0},{1,0,GRHI::VertexFormat::Float3,12},
        {2,1,resident?GRHI::VertexFormat::UInt32:GRHI::VertexFormat::Float2,0}};
    GRHI::GraphicsPipelineDesc desc;desc.VertexShader=vertex;desc.FragmentShader=fragment;desc.ColorFormat=GRHI::TextureFormat::RGBA8_UNorm;
    desc.VertexBindings=bindings;desc.VertexBindingCount=instanced?2:1;desc.VertexAttributes=attributes;desc.VertexAttributeCount=instanced?3:2;
    desc.DepthFormat=GRHI::TextureFormat::D32_Float;desc.DepthTest=useDepth;desc.DepthWrite=useDepth;desc.DepthCompare=GRHI::CompareOp::Less;
    auto* pipeline=f.Keep(f.Device->CreateGraphicsPipeline(desc),"3D pipeline");
    // The pipeline owns native shader state independently from these wrappers.
    f.Destroy(fragment);f.Destroy(vertex);return pipeline;
}

static GRHI::RenderPassDesc PassDesc(GRHI::ITexture*,GRHI::ITexture*);
static void Bind(GRHI::IRenderPass*,GRHI::IGraphicsPipeline*,GRHI::IBuffer*);
static std::array<uint8_t,4> Pixel(const std::vector<uint8_t>&,int,int);
static void InstanceChecks(Fixture& f) {
    auto* color=Color(f);auto* depth=Depth(f);auto* mesh=Buffer(f,MeshData());auto* pipeline=Pipeline(f,true,TestInstanceData::Vertex);
    const float offsets[][2]={{0,0},{-.5f,0},{.5f,0}};
    GRHI::BufferDesc description{sizeof(offsets),GRHI::BufferUsage::Vertex};
    const GRHI::BufferInitialData initial{offsets,sizeof(offsets)};
    auto* instances=f.Keep(f.Device->CreateBuffer(description,&initial),"instance offsets");
    const auto bind=[&](Recording& recording,std::size_t offset=0) {
        recording.Pass=recording.Command->BeginRenderPass(PassDesc(color,depth));
        Check(recording.Pass,"Cannot begin instanced pass");Bind(recording.Pass,pipeline,mesh);
        recording.Pass->SetVertexBuffer(1,instances,offset);
        const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        recording.Pass->SetVertexUniform(0,identity.data(),sizeof(identity));
    };
    std::vector<uint8_t> pixels;
    const auto expect=[&](bool left,bool right) {
        Check(f.Device->ReadTexture(color,pixels),f.Device->GetLastError());
        const auto a=Pixel(pixels,16,32),b=Pixel(pixels,48,32),middle=Pixel(pixels,32,32);
        Check((left?a[1]>115:a[2]>210) && (right?b[1]>115:b[2]>210) && middle[2]>210,
            "Instance input rate, firstInstance, vertex offset or depth changed rendered positions");
    };
    {
        Recording recording(f.Device);bind(recording);recording.Pass->DrawIndexed(6,2,0,0,1);
        Check(recording.Submit(),f.Device->GetLastError());expect(true,true);
    }
    {
        Recording recording(f.Device);bind(recording);recording.Pass->Draw(6,2,0,1);
        Check(recording.Submit(),f.Device->GetLastError());expect(true,true);
    }
    {
        Recording recording(f.Device);bind(recording);recording.Pass->DrawIndexed(6,1,0,0,2);
        Check(recording.Submit(),f.Device->GetLastError());expect(false,true);
    }
    {
        Recording recording(f.Device);bind(recording,sizeof(offsets[0]));recording.Pass->DrawIndexed(6,2);
        Check(recording.Submit(),f.Device->GetLastError());expect(true,true);
    }
    for(unsigned invalid=0;invalid<4;++invalid) {
        Recording recording(f.Device);bind(recording);
        if(invalid==0)recording.Pass->DrawIndexed(6,3,0,0,1);
        if(invalid==1)recording.Pass->Draw(6,2,0,UINT32_MAX);
        if(invalid==2){recording.Pass->SetVertexBuffer(1,instances,sizeof(offsets[0])*2);recording.Pass->DrawIndexed(6,2);}
        if(invalid==3){recording.Pass->SetPipeline(pipeline);recording.Pass->DrawIndexed(6,2);}
        Check(!recording.Submit() && !f.Device->GetLastError().empty(),"Invalid instance span or missing binding reached native draw");
    }
    std::cout<<"PASS instance-rate vertex bindings: indexed/nonindexed draws, nonzero firstInstance and byte offsets, depth and out-of-range rejection\n";
}
static void ResidentUploadChecks(Fixture& f) {
    auto* left=Color(f);auto* right=Color(f);auto* depth=Depth(f);
    auto* mesh=Buffer(f,MeshData());auto* pipeline=Pipeline(f,true,TestInstanceData::Resident);
    auto* offsets=f.Keep(f.Device->CreateBuffer({16,GRHI::BufferUsage::VertexStorage,false}),"resident offsets");
    auto* visible=f.Keep(f.Device->CreateBuffer({4,GRHI::BufferUsage::Vertex,false}),"visible index");
    const auto draw=[&](Recording& recording,GRHI::ITexture* target) {
        recording.Pass=recording.Command->BeginRenderPass(PassDesc(target,depth));
        Check(recording.Pass,"Cannot begin resident-instance pass");
        Bind(recording.Pass,pipeline,mesh);
        recording.Pass->SetVertexBuffer(1,visible);
        recording.Pass->SetVertexStorageBuffer(0,offsets);
        const std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        recording.Pass->SetVertexUniform(0,identity.data(),sizeof(identity));
        recording.Pass->DrawIndexed(6);
        recording.Command->EndRenderPass(recording.Pass);recording.Pass=nullptr;
    };
    {
        Recording recording(f.Device);
        float initialOffsets[4]={-.5f,0,-.5f,0};
        uint32_t index=0;
        const GRHI::BufferUpload first[]={{offsets,0,initialOffsets,sizeof(initialOffsets)},
            {visible,0,&index,sizeof(index),true}};
        Check(recording.Command->UploadBuffers(first,2),f.Device->GetLastError());
        draw(recording,left);
        index=1;
        float newOffset[2]={.5f,0};
        const GRHI::BufferUpload second[]={{offsets,8,newOffset,sizeof(newOffset)},
            {visible,0,&index,sizeof(index),true}};
        Check(recording.Command->UploadBuffers(second,2),f.Device->GetLastError());
        draw(recording,right);
        initialOffsets[0]=0;newOffset[0]=0;index=0;
        Check(recording.Submit(),f.Device->GetLastError());
    }
    std::vector<uint8_t> pixels;
    const auto expect=[&](GRHI::ITexture* target,int drawn,int clear) {
        Check(f.Device->ReadTexture(target,pixels),f.Device->GetLastError());
        Check(Pixel(pixels,drawn,32)[1]>115 && Pixel(pixels,clear,32)[2]>210,
            "Deferred buffer upload order, storage binding or cycling changed the rendered position");
    };
    expect(left,16,48);expect(right,48,16);
    {
        Recording recording(f.Device);
        const uint32_t index=0;
        const GRHI::BufferUpload upload{visible,0,&index,sizeof(index),true};
        Check(recording.Command->UploadBuffers(&upload,1),f.Device->GetLastError());
        draw(recording,left);
        Check(recording.Submit(),f.Device->GetLastError());
    }
    expect(left,16,48);
    {
        Recording recording(f.Device);
        const uint32_t index=0;
        const GRHI::BufferUpload upload{mesh,0,&index,sizeof(index)};
        Check(!recording.Command->UploadBuffers(&upload,1) && !recording.Submit(),
            "Upload mutated an immutable index buffer");
    }
    {
        auto* released=f.Keep(f.Device->CreateBuffer({4,GRHI::BufferUsage::Vertex,false}),"release before upload");
        Recording recording(f.Device);
        const uint32_t index=0;
        const GRHI::BufferUpload upload{released,0,&index,sizeof(index)};
        Check(recording.Command->UploadBuffers(&upload,1),f.Device->GetLastError());
        f.Destroy(released);
        Check(!recording.Submit(),"Upload accepted a destination released after recording");
    }
    std::cout<<"PASS resident storage reads and ordered batched uploads on Metal\n";
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
static void ReplayChecks(Fixture& f) {
    auto* color=Color(f);auto* depth=Depth(f);auto* mesh=Buffer(f,MeshData());
    auto* pipeline=Pipeline(f,true);auto* otherPipeline=Pipeline(f,false);
    Recording recording(f.Device);recording.Pass=recording.Command->BeginRenderPass(PassDesc(color,depth));
    for(unsigned draw=0;draw<3;++draw) {
        Bind(recording.Pass,draw==2?otherPipeline:pipeline,mesh);
        std::array<float,16> matrix{.3f,0,0,0,0,.3f,0,0,0,0,1,0,float(draw)*.5f-.5f,0,0,1};
        std::array<float,4> tint{1,draw==0?1.f:draw==1?.5f:.25f,1,1};
        recording.Pass->SetVertexUniform(0,matrix.data(),sizeof(matrix));
        recording.Pass->SetFragmentUniform(0,tint.data(),sizeof(tint));
        recording.Pass->DrawIndexed(3);
        matrix.fill(0);tint.fill(0);
    }
    Check(recording.Submit(),f.Device->GetLastError());std::vector<uint8_t> pixels;
    Check(f.Device->ReadTexture(color,pixels),f.Device->GetLastError());
    const auto left=Pixel(pixels,16,32),middle=Pixel(pixels,32,32),right=Pixel(pixels,48,32);
    Check(left[1]>245 && middle[1]>120 && middle[1]<135 && right[1]>55 && right[1]<75,
        "Uniform snapshots or replay cache lost per-draw changes across pipeline switches");
    std::cout<<"PASS native replay: distinct per-draw transforms/material uniforms survive caller mutation and pipeline switches\n";
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
    // Repeated ranges hit the same immutable-buffer extrema during recording,
    // final submission validation and later frames. Base vertex stays per draw.
    for(unsigned frame=0;frame<3;++frame) {
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,pipeline,buffer);
        for(unsigned repeat=0;repeat<64;++repeat) {
            r.Pass->DrawIndexed(3);
            r.Pass->DrawIndexed(3,1,3,-3);
        }
        Check(r.Submit(),f.Device->GetLastError());
    }
    Check(f.Device->ReadTexture(color,bytes)&&Pixel(bytes,41,32)==green,"Repeated cached indexed ranges changed rendering");
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
        auto mixed=MeshData();for(auto& index:mixed.Indices)index=65536;
        auto* mixedFormat=Buffer(f,mixed);
        {
            Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,pipeline,mixedFormat);
            r.Pass->SetIndexBuffer(mixedFormat,GRHI::IndexFormat::UInt16,offsetof(Geometry,Indices));r.Pass->DrawIndexed(3);
            Check(r.Submit(),"Valid UInt16 interpretation could not warm the immutable index range");
        }
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,pipeline,mixedFormat);
        r.Pass->DrawIndexed(3);
        Check(!r.Submit(),"UInt32 reused a UInt16 range and bypassed vertex bounds validation");
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
    // These ranges are already cached above. Check both extrema after applying
    // a new vertex offset; max alone would miss the first negative index.
    rejected([](Recording& r){r.Pass->DrawIndexed(3,1,0,-1);});
    rejected([](Recording& r){r.Pass->DrawIndexed(3,1,3,-4);});
    rejected([](Recording& r){r.Pass->DrawIndexed(3,1,3,INT32_MAX);});
    rejected([&](Recording& r){r.Pass->SetIndexBuffer(buffer,GRHI::IndexFormat::UInt32,offsetof(Geometry,Indices)+4);r.Pass->DrawIndexed(6);});
    rejected([&](Recording& r){r.Pass->SetIndexBuffer(buffer,static_cast<GRHI::IndexFormat>(99),offsetof(Geometry,Indices));r.Pass->DrawIndexed(3);});
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
    auto* lateIndex=Buffer(f,MeshData());
    rejected([&](Recording& r){r.Pass->SetIndexBuffer(lateIndex,GRHI::IndexFormat::UInt32,offsetof(Geometry,Indices));r.Pass->DrawIndexed(3);lateIndex->Release();});
    auto* releasedBeforeDraw=Buffer(f,MeshData());
    rejected([&](Recording& r){r.Pass->SetVertexBuffer(0,releasedBeforeDraw);releasedBeforeDraw->Release();r.Pass->DrawIndexed(3);});
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
    {
        auto* unrelated=Buffer(f,MeshData());
        Recording r(f.Device);r.Pass=r.Command->BeginRenderPass(PassDesc(color,depth));Bind(r.Pass,pipeline,buffer);
        r.Pass->DrawIndexed(3);unrelated->Release();r.Pass->DrawIndexed(3);
        Check(r.Submit(),"An unrelated release invalidated live recorded resources");
    }
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

static void SampledDepthChecks(Fixture& f) {
    static const char source[]=R"(
#include <metal_stdlib>
using namespace metal;
struct Output {float4 position [[position]];float2 uv;};
vertex Output depthVertex(uint id [[vertex_id]]) {
    float2 p[3]={float2(-.6,-.6),float2(.6,-.6),float2(0,.6)};
    Output out;out.position=float4(p[id],.2,1);out.uv=float2(0);return out;
}
fragment void depthFragment() {}
vertex Output sampleVertex(uint id [[vertex_id]]) {
    float2 p[3]={float2(-1,1),float2(3,1),float2(-1,-3)};
    float2 uv[3]={float2(0,0),float2(2,0),float2(0,2)};
    Output out;out.position=float4(p[id],0,1);out.uv=uv[id];return out;
}
fragment float4 sampleDepth(Output in [[stage_in]],texture2d<float> depth [[texture(0)]],sampler depthSampler [[sampler(0)]]) {
    float value=depth.sample(depthSampler,in.uv).r;return float4(value,value,value,1);
}
)";
    const auto shader=[&](GRHI::ShaderStage stage,const char* entry,uint32_t samplers) {
        GRHI::ShaderCode code;code.Format=GRHI::ShaderCodeFormat::MSL;code.Data=source;code.DataSize=sizeof(source);
        code.EntryPoint=entry;code.SamplerCount=samplers;
        GRHI::ShaderDesc desc;desc.Stage=stage;desc.Codes=&code;desc.CodeCount=1;
        return f.Keep(f.Device->CreateShader(desc),"sampled depth shader");
    };
    auto* vertex=shader(GRHI::ShaderStage::Vertex,"depthVertex",0);
    auto* fragment=shader(GRHI::ShaderStage::Fragment,"depthFragment",0);
    GRHI::GraphicsPipelineDesc description;description.VertexShader=vertex;description.FragmentShader=fragment;
    description.DepthFormat=GRHI::TextureFormat::D32_Float;description.DepthTest=true;description.DepthWrite=true;
    auto* write=f.Keep(f.Device->CreateGraphicsPipeline(description),"depth-only pipeline");
    Check(write->GetColorFormat()==GRHI::TextureFormat::Unknown,"Depth-only pipeline exposes a color format");
    auto invalid=description;invalid.DepthFormat=GRHI::TextureFormat::Unknown;
    Check(!f.Device->CreateGraphicsPipeline(invalid),"Target-less pipeline accepted");
    invalid=description;invalid.FragmentShader=nullptr;
    Check(!f.Device->CreateGraphicsPipeline(invalid),"Missing SDL fragment shader accepted");
    invalid=description;invalid.Blend=GRHI::BlendMode::Alpha;
    Check(!f.Device->CreateGraphicsPipeline(invalid),"Depth-only blend state accepted");

    description.VertexShader=shader(GRHI::ShaderStage::Vertex,"sampleVertex",0);
    description.FragmentShader=shader(GRHI::ShaderStage::Fragment,"sampleDepth",1);
    description.ColorFormat=GRHI::TextureFormat::RGBA8_UNorm;
    description.DepthFormat=GRHI::TextureFormat::Unknown;description.DepthTest=false;description.DepthWrite=false;
    auto* read=f.Keep(f.Device->CreateGraphicsPipeline(description),"sampled-depth display pipeline");
    description.DepthFormat=GRHI::TextureFormat::D32_Float;
    auto* feedback=f.Keep(f.Device->CreateGraphicsPipeline(description),"depth feedback validation pipeline");
    GRHI::TextureDesc texture;texture.Width=64;texture.Height=64;texture.Format=GRHI::TextureFormat::D32_Float;
    texture.Usage=GRHI::TextureUsage::DepthStencilTarget|GRHI::TextureUsage::Sampled;
    auto* depth=f.Keep(f.Device->CreateTexture(texture),"sampled D32 target");
    Check(depth->GetDepthTargetView().Token&&!depth->GetColorTargetView().Token,"Sampled depth view domain mismatch");
    texture.Usage=GRHI::TextureUsage::Sampled;
    Check(!f.Device->CreateTexture(texture),"Sampled D32 without depth target usage accepted");
    texture.Usage=GRHI::TextureUsage::DepthStencilTarget|GRHI::TextureUsage::ColorTarget;
    Check(!f.Device->CreateTexture(texture),"Depth-as-color usage accepted");
    texture.Usage=GRHI::TextureUsage::DepthStencilTarget|GRHI::TextureUsage::Sampled;
    const float initialDepth=.5f;const GRHI::TextureInitialData initial{&initialDepth,sizeof(initialDepth),0};
    Check(!f.Device->CreateTexture(texture,&initial),"D32 initial pixel upload accepted");
    auto* color=Color(f);
    GRHI::SamplerDesc samplerDesc;samplerDesc.MinFilter=GRHI::FilterMode::Nearest;samplerDesc.MagFilter=GRHI::FilterMode::Nearest;
    auto* sampler=f.Keep(f.Device->CreateSampler(samplerDesc),"nearest depth sampler");
    for(unsigned frame=0;frame<2;++frame) {
        Recording recording(f.Device);
        GRHI::RenderPassDesc pass;pass.DepthTarget=depth->GetDepthTargetView();pass.DepthStore=GRHI::StoreOp::Store;pass.ClearDepth=frame?.75f:.9f;
        recording.Pass=recording.Command->BeginRenderPass(pass);Check(recording.Pass,"Cannot begin depth-only pass");
        recording.Pass->SetPipeline(write);recording.Pass->Draw(3);
        recording.Command->EndRenderPass(recording.Pass);recording.Pass=nullptr;
        // Same command: the second pass must see depth produced by the first,
        // including Store semantics, clear value and the default depth viewport.
        pass={};pass.ColorTarget=color->GetColorTargetView();
        recording.Pass=recording.Command->BeginRenderPass(pass);Check(recording.Pass,"Cannot begin depth sampling pass");
        recording.Pass->SetPipeline(read);recording.Pass->SetFragmentTextureSampler(0,depth,sampler);recording.Pass->Draw(3);
        Check(recording.Submit(),f.Device->GetLastError());
        std::vector<uint8_t> pixels;Check(f.Device->ReadTexture(color,pixels),f.Device->GetLastError());
        const auto center=Pixel(pixels,32,32),outside=Pixel(pixels,1,1);
        Check(center[0]>=49&&center[0]<=53,"Depth-only draw was not stored and sampled as .2");
        Check(frame?(outside[0]>=190&&outside[0]<=193):(outside[0]>=228&&outside[0]<=231),"Depth clear/store or repeated sampling failed");
    }
    {
        Recording recording(f.Device);Check(!recording.Command->BeginRenderPass({}),"Render pass with no attachments accepted");
        Check(!recording.Submit(),"Attachment-less pass reached GPU");
    }
    {
        Recording recording(f.Device);GRHI::RenderPassDesc pass;pass.DepthTarget=depth->GetDepthTargetView();
        recording.Pass=recording.Command->BeginRenderPass(pass);Check(recording.Pass,"Cannot begin depth-only validation pass");
        recording.Pass->SetPipeline(read);Check(!recording.Submit(),"Color pipeline bound to depth-only pass");
    }
    {
        Recording recording(f.Device);recording.Pass=recording.Command->BeginRenderPass(PassDesc(color,depth));
        recording.Pass->SetPipeline(feedback);recording.Pass->SetFragmentTextureSampler(0,depth,sampler);recording.Pass->Draw(3);
        Check(!recording.Submit(),"Depth read/write feedback accepted");
    }
    {
        auto* doomed=f.Keep(f.Device->CreateTexture(texture),"released sampled depth");
        Recording recording(f.Device);GRHI::RenderPassDesc pass;pass.DepthTarget=doomed->GetDepthTargetView();
        recording.Pass=recording.Command->BeginRenderPass(pass);recording.Pass->SetPipeline(write);recording.Pass->Draw(3);
        doomed->Release();Check(!recording.Submit(),"Released depth-only target reached GPU submission");
    }
    std::cout<<"PASS real Metal sampled D32: depth-only draw/clear/store, consecutive-pass sampling, reuse and invalid targets/feedback/lifetimes\n";
}
#endif
int main(int argc,char** argv) {
#if !defined(__APPLE__)
    (void)argc;(void)argv;std::cout<<"SKIP real GPU fixture currently supplies Metal source; API remains platform independent\n";return 77;
#else
    try {Check(argc==2,"Expected runtime binary directory");Fixture fixture;fixture.Api=ExportGRHIApi(argv[1]);Check(fixture.Api,"Cannot load GRHI");fixture.Device=fixture.Api->CreateDevice();Check(fixture.Device,"Cannot create Metal device");GPUChecks(fixture);InstanceChecks(fixture);ResidentUploadChecks(fixture);ReplayChecks(fixture);ImageCompatibility(fixture);SampledDepthChecks(fixture);return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";return 1;}
#endif
}

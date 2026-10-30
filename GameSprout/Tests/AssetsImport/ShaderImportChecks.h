#pragma once
#include "GRHIExport.h"

inline void CheckShaderImports(const QDir & root, ILibraryManager * library,
    const std::function<AssetImportResult(TestMeta &,bool)> & import) {
    std::string error;
    Check(root.mkpath("Assets/Shaders/Includes"),"Cannot create shader fixtures");
    const auto write=[&](const QString & path,const QByteArray & bytes) {Check(AssetArtifactIO::Write(root.filePath("Assets/"+path),bytes,error),error);};
    const auto persist=[&](const TestMeta & meta) {std::string json;Check(meta.SerializeMeta(json),"Cannot serialize shader Meta");write(QString::fromStdString(meta.Path)+".meta",QByteArray::fromStdString(json));};
    write("Shaders/Includes/Shared values.hlsli","float4 ShaderColor() { return float4(TINT,0,0,1); }\n");
    const QByteArray vertex="#define COLOR_FILE \"Includes/Shared values.hlsli\"\n#if ENABLE_COLOR\n#include COLOR_FILE // actual compiler include discovery\n#endif\n#if 0\n#include \"does-not-exist.hlsli\"\n#endif\nfloat4 main(uint id : SV_VertexID) : SV_Position {return ShaderColor()+float4(id,0,0,0);}\n";
    write("Shaders/Test.shader",vertex);
    TestMeta shader(eAssetImporterType::Shader,Guid(13,23,33,43),"Shaders/Test.shader");
    shader.Settings["Stage"]="Vertex";shader.Settings["SourceLanguage"]="HLSL";shader.Settings["Defines"].append("TINT=0.25");
    shader.Settings["Defines"].append("ENABLE_COLOR");
    auto compiled=import(shader,false);Check(compiled.Success,"HLSL vertex compilation failed: "+compiled.Error);persist(shader);
    Check(!compiled.Manifest.ToolchainHash.empty(),"Compiler cache fingerprint missing");
    Check(compiled.Manifest.SettingsHash==AssetArtifactIO::Hash(QByteArray::fromStdString(std::to_string(static_cast<uint16_t>(shader._ImporterType))+":"+AssetArtifactIO::JsonText(shader.Settings))),"Toolchain fingerprint polluted import-settings hash");
    Check(compiled.Manifest.Dependencies.size()==1&&compiled.Manifest.Dependencies[0].Path=="Shaders/Includes/Shared values.hlsli","Conditional/macro include tracking failed");
    QByteArray payload;auto info=ReadArtifact(library->QueryAsset(shader._Guid,compiled.Manifest.Assets[0].LocalID),&payload);
    Check(info["Variants"].size()==2&&info["Variants"][0]["Format"]=="SPIRV"&&info["Variants"][1]["Format"]=="MSL","Shader formats missing");
    const auto msl=info["Variants"][1];
    const QByteArray mslBytes=payload.mid(qsizetype(msl["Offset"].asUInt64()),qsizetype(msl["Size"].asUInt64()));
    Check(payload.startsWith(QByteArray::fromHex("03022307"))&&mslBytes.endsWith(char(0)),"Shader binary/NUL termination invalid");
    auto cached=import(shader,false);Check(cached.Success&&cached.Cached,"HLSL cache miss on unchanged input");
    write("Shaders/Includes/Shared values.hlsli","float4 ShaderColor() { return float4(TINT,0.5,0,1); }\n");
    auto changed=import(shader,false);Check(changed.Success&&!changed.Cached,"Shader include modification did not invalidate cache");
    shader.Settings["Optimize"]=false;shader.Settings["GenerateDebugInfo"]=true;
    auto debug=import(shader,false);Check(debug.Success&&!debug.Cached,"HLSL optimization/debug settings failed: "+debug.Error);persist(shader);
    write("Shaders/Test.shader","this is not valid HLSL");
    auto failed=import(shader,false);AssetImportManifest retained;
    Check(!failed.Success&&failed.Error.find("Shaders/Test.shader")!=std::string::npos&&library->GetManifest(shader._Guid,retained)&&retained.Generation==debug.Manifest.Generation,"Shader diagnostics or failure preservation invalid");
    write("Shaders/Test.shader",vertex);

    write("Shaders/Sample.frag.shader","Texture2D<float4> tex : register(t0,space2);\nSamplerState samp : register(s0,space2);\ncbuffer Params : register(b0,space3) { float4 tint; };\nfloat4 main(float2 uv : TEXCOORD0) : SV_Target0 { return tex.Sample(samp,uv)*tint; }\n");
    TestMeta fragment(eAssetImporterType::Shader,Guid(61,62,63,64),"Shaders/Sample.frag.shader");fragment.Settings["Stage"]="Fragment";
    auto fs=import(fragment,false);Check(fs.Success,"HLSL fragment compilation failed: "+fs.Error);persist(fragment);
    const auto fsInfo=ReadArtifact(library->QueryAsset(fragment._Guid,fs.Manifest.Assets[0].LocalID));
    Check(fsInfo["Resources"]["SamplerCount"].asUInt()==1&&fsInfo["Resources"]["UniformBufferCount"].asUInt()==1,"SDL shader counts invalid");
    Check(fsInfo["Reflection"]["ubos"][0]["block_size"].asUInt()==16,"Uniform byte layout missing");
    write("Shaders/Work.compute.shader","RWStructuredBuffer<float4> result : register(u0,space1);\ncbuffer Params : register(b0,space2) {float4 color;};\n[numthreads(8,4,1)] void main(uint3 id : SV_DispatchThreadID) {result[id.x]=color;}\n");
    TestMeta compute(eAssetImporterType::Shader,Guid(65,66,67,68),"Shaders/Work.compute.shader");
    compute.Settings["Stage"]="Compute";
    auto cs=import(compute,false);Check(cs.Success,"HLSL compute compilation failed: "+cs.Error);persist(compute);
    const auto csInfo=ReadArtifact(library->QueryAsset(compute._Guid,cs.Manifest.Assets[0].LocalID));
    Check(csInfo["ThreadGroupSize"][0].asUInt()==8&&csInfo["ThreadGroupSize"][1].asUInt()==4&&csInfo["Resources"]["ReadWriteStorageBufferCount"].asUInt()==1,"Compute reflection invalid");
    write("Shaders/Bad.shader","Texture2D<float4> tex : register(t2,space2); SamplerState samp : register(s2,space2); float4 main(float2 uv:TEXCOORD0):SV_Target0 {return tex.Sample(samp,uv);}");
    TestMeta bad(eAssetImporterType::Shader,Guid(69,70,71,72),"Shaders/Bad.shader");bad.Settings["Stage"]="Fragment";
    auto rejected=import(bad,false);Check(!rejected.Success&&rejected.Error.find("consecutive")!=std::string::npos,"Sparse SDL bindings were accepted");
    Check(QFile::remove(root.filePath("Assets/Shaders/Bad.shader")),"Cannot remove invalid shader fixture");
    shader.Settings["GenerateDXIL"]=true;
    auto dxil=import(shader,false);
    Check(dxil.Success,"DXIL compilation failed: "+dxil.Error);
    QByteArray dxilPayload;const auto dxilInfo=ReadArtifact(library->QueryAsset(shader._Guid,dxil.Manifest.Assets[0].LocalID),&dxilPayload);
    bool foundDXIL=false;
    for(const auto & variant:dxilInfo["Variants"]) if(variant["Format"]=="DXIL") {
        foundDXIL=dxilPayload.mid(qsizetype(variant["Offset"].asUInt64()),4)=="DXBC";
    }
    Check(foundDXIL,"DXIL artifact bytes missing");
    shader.Settings["GenerateDXIL"]=false;persist(shader);

    // The generated MSL is accepted by the real GRHI/Metal device, without the
    // offline Xcode Metal Toolchain. This shader has no uniform/storage bindings.
    GRHI::GRHIApi * rhi=ExportGRHIApi(TEST_BINARY_ROOT);Check(rhi!=nullptr,"GRHI cannot load for shader test");
    GRHI::IDevice * device=rhi->CreateDevice();Check(device!=nullptr,"GRHI cannot create shader test device");
    GRHI::ShaderCode code;code.Format=GRHI::ShaderCodeFormat::MSL;code.Data=mslBytes.constData();code.DataSize=size_t(mslBytes.size());
    const std::string function=msl["EntryPoint"].asString();code.EntryPoint=function.c_str();
    GRHI::ShaderDesc desc;desc.Codes=&code;desc.CodeCount=1;
    GRHI::IShader * native=device->CreateShader(desc);
    const std::string nativeError=device->GetLastError();
    delete native;delete device;delete rhi;
    Check(native!=nullptr,"Generated MSL failed GRHI device creation: "+nativeError);
    std::cout<<"PASS HLSL vertex/fragment/compute, reflection, macro/conditional includes, cache, diagnostics and native GRHI shader creation\n";
}

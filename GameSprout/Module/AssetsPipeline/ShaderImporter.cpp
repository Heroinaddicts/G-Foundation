#include "AssetImportJob.h"
#include "IShaderCompiler.h"
#include "../Shared/MaterialDefinition.h"

bool ImportShader(AssetImportJob & job) {
    Json::Value materialSchema;
    std::string materialError;
    if (!MaterialDefinition::ParseShaderSchema(job.SourceBytes,materialSchema,materialError)) return job.Fail(materialError);
    if(!job.Request.Compiler) return job.Fail("ShaderCompiler module is unavailable");
    ShaderCompileRequest request;
    request.SourceRoot=job.Request.AssetsRoot;request.SourcePath=job.Request.SourcePath;
    request.Source=job.SourceBytes.toStdString();request.WorkDirectory=job.Stage;
    const std::string stage=job.Settings.get("Stage","Vertex").asString();
    if(stage=="Vertex") request.Stage=eShaderStage::Vertex;
    else if(stage=="Fragment") request.Stage=eShaderStage::Fragment;
    else if(stage=="Compute") request.Stage=eShaderStage::Compute;
    else return job.Fail("Invalid shader stage");
    request.EntryPoint=job.Settings.get("EntryPoint","main").asString();
    request.Optimize=job.Settings.get("Optimize",true).asBool();
    request.GenerateDebugInfo=job.Settings.get("GenerateDebugInfo",false).asBool();
    if(job.Settings.isMember("Defines") && !job.Settings["Defines"].isArray()) return job.Fail("Shader Defines must be an array");
    for(const auto & define:job.Settings["Defines"]) {
        if(!define.isString()) return job.Fail("Shader define must be a string");
        request.Defines.push_back(define.asString());
    }
    if(job.Settings.get("GenerateDXIL",false).asBool()) request.Targets.push_back({eShaderCodeFormat::DXIL,eShaderPlatform::Direct3D12});
    request.IsCancellationRequested=[&job] {return job.Context && job.Context->IsCancellationRequested();};
    ShaderCompileResult result;
    const bool compiled=job.Request.Compiler->Compile(request,result);
    job.Result.Diagnostics=std::move(result.Diagnostics);
    if(!compiled) return job.Fail(result.Error);
    for(const auto & dependency:result.Dependencies) {
        QByteArray bytes;
        if(!job.AddDependency(dependency.Path,&bytes)) return false;
        if(AssetArtifactIO::Hash(bytes)!=dependency.ContentHash) {
            job.Result.Superseded=true;job.Result.Retry=true;
            return job.Fail("Shader include changed before import commit");
        }
    }
    Json::Value descriptor;
    if(!AssetArtifactIO::Parse(result.ReflectionJson,descriptor) || !descriptor.isObject()) return job.Fail("ShaderCompiler returned invalid reflection");
    descriptor["Material"]=materialSchema;
    descriptor["Settings"]=job.Settings;descriptor["ToolchainFingerprint"]=job.Request.Compiler->GetToolchainHash();
    descriptor["Variants"]=Json::Value(Json::arrayValue);
    QByteArray payload;
    for(const auto & code:result.Codes) {
        while(payload.size()%4) payload.append(char(0));
        Json::Value variant;
        switch(code.Format) {
        case eShaderCodeFormat::SPIRV:variant["Format"]="SPIRV";break;
        case eShaderCodeFormat::MSL:variant["Format"]="MSL";break;
        case eShaderCodeFormat::DXIL:variant["Format"]="DXIL";break;
        case eShaderCodeFormat::Metallib:variant["Format"]="MetalLib";break;
        }
        switch(code.Platform) {
        case eShaderPlatform::Vulkan:variant["Platform"]="Vulkan";break;
        case eShaderPlatform::Direct3D12:variant["Platform"]="Direct3D12";break;
        case eShaderPlatform::Apple:variant["Platform"]="Apple";break;
        case eShaderPlatform::MacOS:variant["Platform"]="macOS";break;
        case eShaderPlatform::IOS:variant["Platform"]="iOS";break;
        case eShaderPlatform::IOSSimulator:variant["Platform"]="iOSSimulator";break;
        }
        variant["EntryPoint"]=code.EntryPoint;variant["Offset"]=Json::UInt64(payload.size());variant["Size"]=Json::UInt64(code.Data.size());
        descriptor["Variants"].append(variant);
        payload.append(reinterpret_cast<const char *>(code.Data.data()),qsizetype(code.Data.size()));
    }
    return job.Emit(request.Stage==eShaderStage::Compute?eAssetType::ComputeShader:eAssetType::Shader,
        "main",QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString(),descriptor,payload);
}

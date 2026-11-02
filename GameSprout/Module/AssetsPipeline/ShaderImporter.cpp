#include "AssetImportJob.h"
#include "IShaderCompiler.h"
#include "../Shared/MaterialDefinition.h"
#include "../Shared/ShaderDefinition.h"

static bool ImportComputeShader(AssetImportJob & job) {
    Json::Value materialSchema;
    std::string materialError;
    if (!MaterialDefinition::ParseShaderSchema(job.SourceBytes,materialSchema,materialError)) return job.Fail(materialError);
    if(!job.Request.Compiler) return job.Fail("ShaderCompiler module is unavailable");
    ShaderCompileRequest request;
    request.SourceRoot=job.Request.AssetsRoot;request.SourcePath=job.Request.SourcePath;
    request.Source=job.SourceBytes.toStdString();request.WorkDirectory=job.Stage;
    request.Stage=eShaderStage::Compute;
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
    return job.Emit(eAssetType::ComputeShader,
        "main",QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString(),descriptor,payload);
}

namespace {
bool AppendProgram(AssetImportJob& job,const ShaderCompileRequest& request,const Json::Value& schema,
    QByteArray& payload,Json::Value& program) {
    ShaderCompileResult result;
    const bool compiled=job.Request.Compiler->Compile(request,result);
    job.Result.Diagnostics+=result.Diagnostics;
    if(!compiled)return job.Fail(result.Error);
    for(const auto& dependency:result.Dependencies) {
        QByteArray bytes;if(!job.AddDependency(dependency.Path,&bytes))return false;
        if(AssetArtifactIO::Hash(bytes)!=dependency.ContentHash){job.Result.Superseded=true;job.Result.Retry=true;return job.Fail("Shader include changed before import commit");}
    }
    Json::Value description;std::string error;
    if(!AssetArtifactIO::Parse(result.ReflectionJson,description)||!description.isObject())return job.Fail("Invalid ShaderCompiler reflection");
    if(!ShaderDefinition::ProgramLayout(description["Reflection"],schema,request.Stage==eShaderStage::Vertex,program,error))return job.Fail(error);
    program["Stage"]=request.Stage==eShaderStage::Vertex?"Vertex":"Fragment";
    program["Reflection"]=description["Reflection"];
    program["Variants"]=Json::Value(Json::arrayValue);
    for(const auto& code:result.Codes) {
        while(payload.size()%4)payload.append(char(0));
        Json::Value variant;
        const char* formats[]={"SPIRV","MSL","DXIL","MetalLib"};
        const char* platforms[]={"Vulkan","Direct3D12","Apple","macOS","iOS","iOSSimulator"};
        variant["Format"]=formats[static_cast<unsigned>(code.Format)];variant["Platform"]=platforms[static_cast<unsigned>(code.Platform)];
        variant["EntryPoint"]=code.EntryPoint;variant["Offset"]=Json::UInt64(payload.size());variant["Size"]=Json::UInt64(code.Data.size());
        program["Variants"].append(variant);payload.append(reinterpret_cast<const char*>(code.Data.data()),qsizetype(code.Data.size()));
    }
    return true;
}

}
bool ImportShader(AssetImportJob& job) {
    if(job.Request.Importer==eAssetImporterType::ComputeShader)return ImportComputeShader(job);
    if(!job.Request.Compiler)return job.Fail("ShaderCompiler module is unavailable");
    Json::Value declaration,schema;std::string error;
    if(!ShaderDefinition::Parse(job.SourceBytes,declaration,error)||!MaterialDefinition::ParseShaderSchema(job.SourceBytes,schema,error))return job.Fail(job.Request.SourcePath+": "+error);
    ShaderCompileRequest request;
    request.SourceRoot=job.Request.AssetsRoot;request.SourcePath=job.Request.SourcePath;request.Source=job.SourceBytes.toStdString();
    request.Optimize=job.Settings.get("Optimize",true).asBool();request.GenerateDebugInfo=job.Settings.get("GenerateDebugInfo",false).asBool();
    if(job.Settings.isMember("Defines")&&!job.Settings["Defines"].isArray())return job.Fail("Shader Defines must be an array");
    for(const auto& define:job.Settings["Defines"]){if(!define.isString())return job.Fail("Shader define must be a string");request.Defines.push_back(define.asString());}
    if(job.Settings.get("GenerateDXIL",false).asBool())request.Targets.push_back({eShaderCodeFormat::DXIL,eShaderPlatform::Direct3D12});
    request.IsCancellationRequested=[&job]{return job.Context&&job.Context->IsCancellationRequested();};
    Json::Value descriptor;descriptor["Format"]="GameSproutShader";descriptor["ShaderVersion"]=1;descriptor["Name"]=declaration["Name"];
    descriptor["Material"]=schema;descriptor["Settings"]=job.Settings;descriptor["ToolchainFingerprint"]=job.Request.Compiler->GetToolchainHash();
    descriptor["Passes"]=Json::Value(Json::arrayValue);QByteArray payload;
    for(const auto& sourcePass:declaration["Passes"]) {
        Json::Value pass=sourcePass;
        for(const char* stage:{"Vertex","Fragment"}) {
            if(job.Cancelled())return false;
            request.Stage=std::string(stage)=="Vertex"?eShaderStage::Vertex:eShaderStage::Fragment;
            request.EntryPoint=sourcePass[stage].asString();
            auto stageRequest=request;stageRequest.Defines.push_back(std::string(stage)=="Vertex"?"GAMESPROUT_VERTEX=1":"GAMESPROUT_FRAGMENT=1");
            request.WorkDirectory=QDir(QString::fromStdString(job.Stage)).filePath(QString::fromStdString(sourcePass["Name"].asString()+"-"+stage)).toStdString();
            stageRequest.WorkDirectory=request.WorkDirectory;
            Json::Value program;if(!AppendProgram(job,stageRequest,schema,payload,program))return false;pass[stage]=std::move(program);
        }
        if(!ShaderDefinition::ValidateLinkedPass(pass,error))return job.Fail(error);
        descriptor["Passes"].append(std::move(pass));
    }
    return job.Emit(eAssetType::Shader,"main",QFileInfo(QString::fromStdString(job.Request.SourcePath)).completeBaseName().toStdString(),descriptor,payload);
}

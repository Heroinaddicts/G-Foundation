#include "AssetToolProcess.h"
#include <QRegularExpression>

bool ImportShader(AssetImportJob & job) {
    const bool compute=job.Request.Importer==eAssetImporterType::ComputeShader;
    const QString extension=QFileInfo(QString::fromStdString(job.Request.SourcePath)).suffix().toLower();
    const QString entry=QString::fromStdString(job.Settings.get("EntryPoint","main").asString());
    QString stage=compute?"compute":QString::fromStdString(job.Settings.get("Stage",extension=="frag"?"Fragment":"Vertex").asString()).toLower();
    if(stage!="vertex" && stage!="fragment" && stage!="compute") return job.Fail("Invalid shader stage");
    if(compute!=(stage=="compute")) return job.Fail("Compute stage requires a ComputeShader source");
    if(entry.isEmpty()) return job.Fail("Shader entrypoint is empty");
    QString language=QString::fromStdString(job.Settings.get("SourceLanguage","Auto").asString()).toUpper();
    if(language=="AUTO") {
        if(extension=="spv") language="SPIRV";
        else if(extension=="metal") language="MSL";
        else if(extension=="glsl"||extension=="vert"||extension=="frag"||extension=="comp") language="GLSL";
        else language="HLSL";
    }
    std::set<std::string> seen{job.Request.SourcePath};
    if(language!="SPIRV" && !CollectShaderIncludes(job,job.Request.SourcePath,job.SourceBytes,seen)) return false;
    const QDir stageDirectory(QString::fromStdString(job.Stage));
    const QString snapshot=stageDirectory.filePath("shader-input");
    const QString input=QDir(snapshot).filePath(QString::fromStdString(job.Request.SourcePath));
    if(!QDir().mkpath(QFileInfo(input).absolutePath()) || !AssetArtifactIO::Write(input,job.SourceBytes,job.Result.Error)) return false;
    for(const auto & dependency:job.Result.Manifest.Dependencies) {
        QByteArray bytes;
        if(!job.AddDependency(dependency.Path,&bytes)) return false;
        const QString output=QDir(snapshot).filePath(QString::fromStdString(dependency.Path));
        if(!QDir().mkpath(QFileInfo(output).absolutePath()) || !AssetArtifactIO::Write(output,bytes,job.Result.Error)) return false;
    }
    QStringList defines;
    if(job.Settings["Defines"].isArray()) for(const Json::Value & value:job.Settings["Defines"]) {
        if(!value.isString() || value.asString().empty()) return job.Fail("Invalid shader define");
        defines << "-D"+QString::fromStdString(value.asString());
    }
    Json::Value metadata;
    metadata["Stage"]=stage.toStdString();metadata["Settings"]=job.Settings;
    metadata["Variants"]=Json::Value(Json::arrayValue);
    QByteArray payload;
    auto variant=[&](const std::string & format,const std::string & platform,const QString & function,const QByteArray & bytes) {
        Json::Value item;item["Format"]=format;item["Platform"]=platform;item["EntryPoint"]=function.toStdString();
        item["Offset"]=static_cast<Json::UInt64>(payload.size());item["Size"]=static_cast<Json::UInt64>(bytes.size());
        metadata["Variants"].append(item);payload+=bytes;
    };
    if(language=="MSL") {
#if defined(__APPLE__)
        const QRegularExpression entryPattern("\\b"+QString(stage=="compute"?"kernel":stage)+"\\s+\\w+\\s+"+QRegularExpression::escape(entry)+"\\s*\\(");
        if (!entryPattern.match(QString::fromUtf8(job.SourceBytes)).hasMatch()) return job.Fail("Metal entrypoint/stage was not found in source: "+entry.toStdString());
        const QString air=stageDirectory.filePath("shader.air"),library=stageDirectory.filePath("shader.metallib");
        QStringList arguments{"-sdk","macosx","metal","-x","metal","-c",input,"-o",air,"-I",snapshot,"-I",QFileInfo(input).absolutePath()};
        arguments << defines << (job.Settings.get("Optimize",true).asBool()?"-O2":"-O0");
        if(job.Settings.get("GenerateDebugInfo",false).asBool()) arguments << "-gline-tables-only";
        if(!AssetToolProcess::Run(job,AssetToolProcess::Find("xcrun"),arguments)
            || !AssetToolProcess::Run(job,AssetToolProcess::Find("xcrun"),{"-sdk","macosx","metallib",air,"-o",library})) return false;
        QByteArray bytes;
        if(!AssetToolProcess::Read(library,bytes)||!bytes.startsWith("MTLB")) return job.Fail("Metal compiler produced no valid library");
        variant("MetalLib","macOS",entry,bytes);
        QFile::remove(air);QFile::remove(library);
#else
        return job.Fail("Native MSL compilation requires the Apple Metal toolchain");
#endif
    } else {
        QByteArray spirv;
        const QString bytecode=stageDirectory.filePath("shader.spv");
        const QString cross=AssetToolProcess::Find("shadercross");
        if(language=="SPIRV") spirv=job.SourceBytes;
        else if(language=="GLSL") {
            const QString compiler=AssetToolProcess::Find("glslangValidator");
            if(compiler.isEmpty()) return job.Fail("Install/deploy glslangValidator for GLSL import");
            QStringList args{"-V","-S",stage=="vertex"?"vert":stage=="fragment"?"frag":"comp","-e",entry,"-o",bytecode,"-I"+snapshot,"-I"+QFileInfo(input).absolutePath()};
            args << defines << (job.Settings.get("Optimize",true).asBool()?"-Os":"-Od");
            if(job.Settings.get("GenerateDebugInfo",false).asBool()) args << "-g";
            args << input;
            if(!AssetToolProcess::Run(job,compiler,args)||!AssetToolProcess::Read(bytecode,spirv)) return false;
        } else if(language=="HLSL") {
            if(cross.isEmpty()) return job.Fail("Install/deploy SDL_shadercross and its DXC dependency for HLSL import");
            if(!job.Settings.get("Optimize",true).asBool()) return job.Fail("SDL_shadercross CLI does not expose disabling HLSL optimization");
            QStringList args{input,"-s","HLSL","-d","SPIRV","-t",stage,"-e",entry,"-o",bytecode,"-I",snapshot,"-I",QFileInfo(input).absolutePath()};
            args<<defines;
            if(job.Settings.get("GenerateDebugInfo",false).asBool()) args<<"-g";
            if(!AssetToolProcess::Run(job,cross,args)||!AssetToolProcess::Read(bytecode,spirv)) return false;
        } else return job.Fail("Unsupported shader source language");
        if(spirv.size()<20 || spirv.size()%4!=0 || spirv.left(4)!=QByteArray::fromHex("03022307")) return job.Fail("Invalid SPIR-V module");
        variant("SPIRV","Vulkan",entry,spirv);
        if(!cross.isEmpty()) {
            if(!AssetArtifactIO::Write(bytecode,spirv,job.Result.Error)) return false;
            const QString msl=stageDirectory.filePath("shader.msl"),reflection=stageDirectory.filePath("shader-reflection.json");
            if(!AssetToolProcess::Run(job,cross,{bytecode,"-s","SPIRV","-d","MSL","-t",stage,"-e",entry,"-o",msl})
                ||!AssetToolProcess::Run(job,cross,{bytecode,"-s","SPIRV","-d","JSON","-t",stage,"-e",entry,"-o",reflection})) return false;
            QByteArray mslBytes,json;
            if(!AssetToolProcess::Read(msl,mslBytes)||!AssetToolProcess::Read(reflection,json)||!AssetArtifactIO::Parse(json.toStdString(),metadata["Reflection"])) return job.Fail("Shader reflection output is invalid");
            const QRegularExpression function(R"(\b(?:vertex|fragment|kernel)\s+\w+\s+(\w+)\s*\()");
            const auto match=function.match(QString::fromUtf8(mslBytes));
            if(!match.hasMatch()) return job.Fail("Cannot resolve translated Metal entrypoint");
            variant("MSL","Apple",match.captured(1),mslBytes);
            QFile::remove(msl);QFile::remove(reflection);
        }
        QFile::remove(bytecode);
    }
    QDir(snapshot).removeRecursively();
    return job.Emit(compute?eAssetType::ComputeShader:eAssetType::Shader,"main",QFileInfo(input).completeBaseName().toStdString(),metadata,payload);
}

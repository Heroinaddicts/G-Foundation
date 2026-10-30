#include "ShaderCompileJob.h"
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>

static QStringList DXCArguments(const ShaderCompileJob & job,const QString & input,const QString & output,bool spirv) {
    const char * profile=job.Request.Stage==eShaderStage::Vertex?"vs_6_0":job.Request.Stage==eShaderStage::Fragment?"ps_6_0":"cs_6_0";
    QStringList args{"-T",profile,"-E",QString::fromStdString(job.Request.EntryPoint),"-Fo",output,job.Request.Optimize?"-O3":"-Od"};
    if(spirv) args<<"-spirv"<<"-fvk-use-dx-layout"<<"-fspv-preserve-interface"<<"-fspv-preserve-bindings";
    if(job.Request.GenerateDebugInfo) {args<<"-Zi";if(spirv) args<<"-fspv-debug=line";else args<<"-Qembed_debug";}
    args<<input;return args;
}
static bool MetalLibrary(ShaderCompileJob & job,const QString & msl,eShaderPlatform platform,QByteArray & output) {
#if defined(__APPLE__)
    const QString sdk=platform==eShaderPlatform::MacOS?"macosx":platform==eShaderPlatform::IOS?"iphoneos":"iphonesimulator";
    const QDir work(job.Work);const QString air=work.filePath(sdk+".air"),library=work.filePath(sdk+".metallib");
    const QString xcrun=QStandardPaths::findExecutable("xcrun");
    const QString standard=platform==eShaderPlatform::MacOS?"-std=macos-metal2.1":"-std=ios-metal2.1";
    QStringList args{"-sdk",sdk,"metal",standard,"-c",msl,"-o",air,job.Request.Optimize?"-O2":"-O0"};
    if(job.Request.GenerateDebugInfo) args<<"-gline-tables-only";
    if(!job.Run(xcrun,args) || !job.Run(xcrun,{"-sdk",sdk,"metallib",air,"-o",library})) return false;
    return (job.Read(library,output) && output.startsWith("MTLB")) || job.Fail("Metal toolchain produced an invalid Metallib");
#else
    (void)msl;(void)platform;(void)output;
    return job.Fail("Metallib requires the corresponding Apple SDK and Metal Toolchain on macOS");
#endif
}
bool CompileShaderTargets(ShaderCompileJob & job,Json::Value & descriptor) {
    const QDir work(job.Work);const QString entry=QString::fromStdString(job.Request.EntryPoint);
    const QString spv=work.filePath("shader.spv"),reflection=work.filePath("reflection.json");
    if(!job.Run(job.Tools.DXC,DXCArguments(job,job.Input,spv,true))) return false;
    QByteArray spirv;
    if(!job.Read(spv,spirv) || spirv.size()<20 || spirv.size()%4 || !spirv.startsWith(QByteArray::fromHex("03022307"))) return job.Fail("DXC produced invalid SPIR-V");
    const QString stage=job.Stage=="vertex"?"vert":job.Stage=="fragment"?"frag":"comp";
    if(!job.Run(job.Tools.Reflect,{spv,"--reflect","--entry",entry,"--stage",stage,"--output",reflection})) return false;
    QByteArray json;
    if(!job.Read(reflection,json) || !ParseShaderJson(json,descriptor["Reflection"])) return job.Fail("Invalid SPIR-V reflection");
    if(!ValidateShaderReflection(job,descriptor["Reflection"])) return false;
    if(!job.Run(job.Tools.Cross,{spv,"-s","SPIRV","-d","JSON","-t",job.Stage,"-e",entry,"-o",reflection})) return false;
    if(!job.Read(reflection,json) || !ParseShaderJson(json,descriptor["SDLReflection"])) return job.Fail("Invalid SDL shader reflection");
    const auto & sdl=descriptor["SDLReflection"];
    for(const auto & field:std::initializer_list<std::pair<const char *,const char *>>{
        {"samplers","SamplerCount"},{"uniform_buffers","UniformBufferCount"},{"storage_textures","StorageTextureCount"},{"storage_buffers","StorageBufferCount"},
        {"readonly_storage_textures","ReadOnlyStorageTextureCount"},{"readonly_storage_buffers","ReadOnlyStorageBufferCount"},
        {"readwrite_storage_textures","ReadWriteStorageTextureCount"},{"readwrite_storage_buffers","ReadWriteStorageBufferCount"}}) {
        if(!sdl.isMember(field.first)) continue;
        if(!sdl[field.first].isUInt()) return job.Fail("Invalid SDL shader resource count");
        descriptor["Resources"][field.second]=sdl[field.first];
    }
    if(job.Request.Stage==eShaderStage::Compute) for(const char * dimension:{"threadcount_x","threadcount_y","threadcount_z"}) {
        if(!sdl[dimension].isUInt() || sdl[dimension].asUInt()==0) return job.Fail("Invalid compute threadgroup size");
        descriptor["ThreadGroupSize"].append(sdl[dimension]);
    }
    descriptor["Stage"]=job.Stage.toStdString();descriptor["SourceLanguage"]="HLSL";
    descriptor["SPIRVVersion"]=qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(spirv.constData()+4));
    descriptor["ShaderModel"]="6.0";descriptor["MSLVersion"]="2.1";
    QByteArray msl;QString metalEntry;const QString metalFile=work.filePath("shader.metal");
    for(const auto & target:job.Request.Targets) {
        if(job.Cancelled()) return false;
        QByteArray code;QString function=entry;
        switch(target.Format) {
        case eShaderCodeFormat::SPIRV:code=spirv;break;
        case eShaderCodeFormat::MSL:
        case eShaderCodeFormat::Metallib: {
            if(msl.isEmpty()) {
                if(!job.Run(job.Tools.Cross,{spv,"-s","SPIRV","-d","MSL","-t",job.Stage,"-e",entry,"--msl-version","2.1.0","-o",metalFile}) || !job.Read(metalFile,msl)) return false;
                const auto match=QRegularExpression("\\b"+QString(job.Stage=="compute"?"kernel":job.Stage)+"\\s+\\w+\\s+(\\w+)\\s*\\(").match(QString::fromUtf8(msl));
                if(msl.isEmpty() || msl.contains(char(0)) || !match.hasMatch()) return job.Fail("Invalid generated MSL or entrypoint");
                metalEntry=match.captured(1);
            }
            function=metalEntry;
            if(target.Format==eShaderCodeFormat::MSL) {code=msl;code.append(char(0));}
            else if(!MetalLibrary(job,metalFile,target.Platform,code)) return false;
            break;
        }
        case eShaderCodeFormat::DXIL: {
            // Use the official SPIR-V -> HLSL roundtrip to retain SDL layouts,
            // then DXC CLI so Optimize/debug settings apply to the final output.
            const QString hlsl=work.filePath("roundtrip.hlsl"),dxil=work.filePath("shader.dxil");
            if(!job.Run(job.Tools.Cross,{spv,"-s","SPIRV","-d","HLSL","-t",job.Stage,"-e",entry,"-o",hlsl})
                || !job.Run(job.Tools.DXC,DXCArguments(job,hlsl,dxil,false)) || !job.Read(dxil,code)) return false;
            if(!code.startsWith("DXBC")) return job.Fail("DXC produced invalid DXIL");
            break;
        }
        }
        ShaderCompiledCode output{target.Format,target.Platform,function.toStdString(),{}};
        output.Data.assign(reinterpret_cast<const uint8_t *>(code.constData()),reinterpret_cast<const uint8_t *>(code.constData()+code.size()));
        job.Result.Codes.push_back(std::move(output));
    }
    return true;
}

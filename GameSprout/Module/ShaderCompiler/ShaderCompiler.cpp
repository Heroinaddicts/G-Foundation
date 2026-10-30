#include "ShaderCompileJob.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <set>

static QString FindTool(const QString & name) {
#if defined(_WIN32)
    const QString executable=name+".exe";
#else
    const QString executable=name;
#endif
    const QDir app(QCoreApplication::applicationDirPath());
    for(const QString & path:{app.filePath("local_tools/shaders/bin/"+executable),QDir(QString::fromUtf8(GAMESPROUT_SHADER_TOOLS_ROOT)).filePath("bin/"+executable)})
        if(QFileInfo(path).isExecutable()) return path;
    return QStandardPaths::findExecutable(executable);
}
bool ShaderCompiler::Initialize(GameSprout::GameSproutApi *) {
    _Tools={FindTool("dxc"),FindTool("shadercross"),FindTool("spirv-cross")};
    QByteArray identity("SDL-DXC-ShaderCompiler-1");
    QFile versions(QDir(QFileInfo(_Tools.Cross).absolutePath()).filePath("../toolchain.json"));
    if(versions.open(QIODevice::ReadOnly)) identity+=versions.readAll();
    for(const QString & path:{_Tools.DXC,_Tools.Cross,_Tools.Reflect}) {
        const QFileInfo file(path);identity+=file.fileName().toUtf8()+QByteArray::number(file.size())+QByteArray::number(file.lastModified().toMSecsSinceEpoch());
    }
    _ToolchainHash=QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex().toStdString();
    return true; // Missing host tools fail individual imports, not editor startup.
}
bool ShaderCompiler::Compile(const ShaderCompileRequest & request,ShaderCompileResult & result) const {
    result=ShaderCompileResult();
    const QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_]*$");
    const QString path=QDir::fromNativeSeparators(QString::fromStdString(request.SourcePath));
    if(request.Source.empty() || request.Source.find('\0')!=std::string::npos || path.contains(QChar('\0')) || !QDir::isRelativePath(path) || path.isEmpty()
        || QDir::cleanPath(path)!=path || path==".." || path.startsWith("../") || !QDir(QString::fromStdString(request.SourceRoot)).exists()
        || !identifier.match(QString::fromStdString(request.EntryPoint)).hasMatch() || static_cast<unsigned>(request.Stage)>2 || request.Targets.empty()) {
        result.Error="Invalid HLSL source, path, stage, entrypoint or targets";return false;
    }
    std::set<std::string> names;
    for(const auto & define:request.Defines) {
        const QString value=QString::fromStdString(define),name=value.section('=',0,0);
        if(!identifier.match(name).hasMatch() || value.contains('\n') || value.contains('\r') || value.contains(QChar('\0')) || !names.insert(name.toStdString()).second) {
            result.Error="Invalid or duplicate shader define: "+define;return false;
        }
    }
    std::set<std::pair<eShaderCodeFormat,eShaderPlatform>> targets;
    for(const auto & target:request.Targets) {
        const bool valid=(target.Format==eShaderCodeFormat::SPIRV && target.Platform==eShaderPlatform::Vulkan)
            || (target.Format==eShaderCodeFormat::DXIL && target.Platform==eShaderPlatform::Direct3D12)
            || (target.Format==eShaderCodeFormat::MSL && target.Platform==eShaderPlatform::Apple)
            || (target.Format==eShaderCodeFormat::Metallib && (target.Platform==eShaderPlatform::MacOS || target.Platform==eShaderPlatform::IOS || target.Platform==eShaderPlatform::IOSSimulator));
        if(!valid || !targets.emplace(target.Format,target.Platform).second) {result.Error="Invalid or duplicate shader target";return false;}
    }
    if(_Tools.DXC.isEmpty() || _Tools.Cross.isEmpty() || _Tools.Reflect.isEmpty()) {result.Error="SDL shader tools are missing. Build/install GameSprout/Tools/ShaderTools (DXC + SDL_shadercross + SPIRV-Cross).";return false;}
    if(!QDir().mkpath(QString::fromStdString(request.WorkDirectory))) {result.Error="Cannot create shader work directory";return false;}
    QTemporaryDir temporary(QDir(QString::fromStdString(request.WorkDirectory)).filePath("shader-XXXXXX"));
    if(!temporary.isValid()) {result.Error="Cannot create isolated shader work directory";return false;}
    ShaderCompileJob job{request,_Tools,{},temporary.path(),{},request.Stage==eShaderStage::Vertex?"vertex":request.Stage==eShaderStage::Fragment?"fragment":"compute"};
    try {
        Json::Value descriptor;
        if(!job.Cancelled() && PrepareShaderSource(job) && CompileShaderTargets(job,descriptor) && !job.Cancelled()) {
            job.Result.ReflectionJson=ShaderJsonText(descriptor);job.Result.Success=true;
        }
    } catch(const std::exception & error) {job.Fail(error.what());}
    catch(...) {job.Fail("Unexpected shader compilation exception");}
    if(!job.Result.Success) {job.Result.Codes.clear();job.Result.ReflectionJson="{}";}
    result=std::move(job.Result);return result.Success;
}

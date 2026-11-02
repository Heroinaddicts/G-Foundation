#include "CodeEditorXcodeBackend.h"
#if defined(Q_OS_MACOS)
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <map>
#include <set>
#include <signal.h>
#include <unistd.h>

namespace {
QString CMakeString(QString text) {
    text.replace('\\',"\\\\");text.replace('"',"\\\"");text.replace('$',"\\$");
    return '"'+text+'"';
}
bool SafeText(const QString& text) {
    return !text.contains(QChar(0))&&!text.contains('\n')&&!text.contains('\r')&&!text.contains(';');
}
bool Write(const QString& path,const QByteArray& bytes,bool& changed,std::string& error) {
    const QFileInfo info(path);
    if(info.isSymLink()) {error="生成文件不能是符号链接："+path.toStdString();return false;}
    QFile previous(path);
    if(previous.open(QIODevice::ReadOnly)&&previous.readAll()==bytes) return true;
    QSaveFile output(path);output.setDirectWriteFallback(false);
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit()) {
        error="无法写入代码工程："+path.toStdString()+"\n"+output.errorString().toStdString();return false;
    }
    changed=true;return true;
}
QByteArray Read(const QString& path) {
    QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();
}
bool ResetRelocatedCache(const QString& directory,std::string& error) {
    const QString cachePath=QDir(directory).filePath("CMakeCache.txt");
    const QByteArray bytes=Read(cachePath);
    if(bytes.isEmpty()) return true;
    const QByteArray expected="CMAKE_HOME_DIRECTORY:INTERNAL="+directory.toUtf8();
    if(bytes.split('\n').contains(expected)) return true;
    if(QFileInfo(cachePath).isSymLink()||!QFile::remove(cachePath)) {
        error="无法重建已移动的 Xcode CMake 缓存。";return false;
    }
    const QString files=QDir(directory).filePath("CMakeFiles");
    if(QFileInfo(files).isSymLink()||(QFileInfo::exists(files)&&!QDir(files).removeRecursively())) {
        error="无法清理已移动工程的 CMakeFiles 缓存。";return false;
    }
    return true;
}
bool ValidateGeneratedTree(const QString& directory,std::string& error) {
    // CMake also writes project workspaces and shared schemes. Reject links in
    // those managed paths instead of following a link outside this cache tree.
    QDirIterator entries(directory,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(entries.hasNext()) {
        const QFileInfo entry(entries.next());
        if(entry.isSymLink()) {error="Xcode 工程缓存不能包含符号链接："+entry.filePath().toStdString();return false;}
    }
    return true;
}
class ChildProcess final : public QProcess {
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
protected:
    void setupChildProcess() override {::setsid();}
#endif
};
bool Run(const QString& executable,const QStringList& arguments,const QString& directory,
         QString& diagnostics,std::string& error,const std::function<bool()>& cancelled) {
    if(cancelled&&cancelled()) {error="代码工程操作已取消。";return false;}
    ChildProcess process;
    process.setWorkingDirectory(directory);process.setProcessChannelMode(QProcess::MergedChannels);
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
    process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#elif QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    process.setChildProcessModifier([]{::setsid();});
#endif
    process.start(executable,arguments);
    if(!process.waitForStarted(5000)) {error="无法启动 "+executable.toStdString()+"："+process.errorString().toStdString();return false;}
    const qint64 processID=process.processId();
    QElapsedTimer timer;timer.start();
    while(process.state()!=QProcess::NotRunning) {
        if((cancelled&&cancelled())||timer.elapsed()>120000) {
            // CMake/xcrun can own children. Wait until the whole generation tree
            // exits before the caller releases its per-project writer lock.
            ::kill(-static_cast<pid_t>(processID),SIGKILL);
            process.kill();process.waitForFinished(-1);
            while(::kill(-static_cast<pid_t>(processID),0)==0) ::usleep(1000);
            error=timer.elapsed()>120000?"生成或打开 Xcode 工程超时。":"代码工程操作已取消。";
            diagnostics+=QString::fromUtf8(process.readAll());return false;
        }
        process.waitForFinished(50);
        diagnostics+=QString::fromUtf8(process.readAll());
        if(diagnostics.size()>4*1024*1024) diagnostics=diagnostics.right(4*1024*1024);
    }
    diagnostics+=QString::fromUtf8(process.readAll());
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0) {
        error="Xcode 工程操作失败：\n"+diagnostics.toStdString();return false;
    }
    return true;
}
bool Paths(const CodeEditorProjectRequest& request,QString& root,QString& assets,QString& directory,
           QStringList& sources,QStringList& headers,std::string& error) {
    root=QFileInfo(request.ProjectRoot).canonicalFilePath();
    assets=QFileInfo(QDir(root).filePath("Assets")).canonicalFilePath();
    if(root.isEmpty()||assets.isEmpty()||!QFileInfo(assets).isDir()||!assets.startsWith(root+'/')) {
        error="项目目录或 Assets 目录无效。";return false;
    }
    for(const QString& text:{root,assets,request.RuntimeSDKDirectory,request.CMakeProgram,request.CompilerProgram,
        request.BuildToolProgram,request.BaseFlags,request.ConfigurationFlags}) {
        if(!SafeText(text)) {error="工程路径或工具链参数含有不支持的换行、NUL 或分号。";return false;}
    }
    if(!QFileInfo(request.CMakeProgram).isExecutable()||!QFileInfo(request.CompilerProgram).isExecutable()||
       !QFileInfo(request.BuildToolProgram).isExecutable()||!QFileInfo(QDir(request.RuntimeSDKDirectory).filePath("RuntimeScriptModule.h")).isFile()) {
        error="代码工程工具链未就绪，请先构建 GameSproutScriptBuild 和 Runtime SDK。";return false;
    }
    directory=root;
    for(const QString& part:{QString("Library"),QString("ScriptBuild"),QString("Xcode")}) {
        directory=QDir(directory).filePath(part);
        if(QFileInfo(directory).isSymLink()) {error="代码工程缓存目录不能是符号链接。";return false;}
    }
    if(!QDir().mkpath(directory)) {error="无法创建固定 Xcode 工程目录。";return false;}
    directory=QFileInfo(directory).canonicalFilePath();
    std::set<QString> names;
    std::map<QString,QString> spellings;
    for(const QString& source:request.Sources) {
        const QString relative=QDir::cleanPath(source);
        const QString absolute=QDir(assets).filePath(relative);
        const QFileInfo info(absolute);
        const QString canonical=info.canonicalFilePath();
        const QString suffix=info.suffix().toLower();
        if(!SafeText(relative)||QDir::isAbsolutePath(relative)||relative==".."||relative.startsWith("../")||
           canonical.isEmpty()||!canonical.startsWith(assets+'/')||!info.isFile()||info.isSymLink()) {
            error="代码源文件不存在或不位于 Assets 内："+source.toStdString();return false;
        }
        static const std::set<QString> extensions={"h","hpp","hxx","inl","cpp","cc","cxx"};
        if(!extensions.contains(suffix)) {error="不支持的代码文件："+source.toStdString();return false;}
        const QString folded=relative.normalized(QString::NormalizationForm_C).toCaseFolded();
        if(!names.insert(folded).second) {error="代码文件路径重复或大小写冲突："+source.toStdString();return false;}
        QString prefix;
        for(const QString& part:relative.split('/')) {
            if(!prefix.isEmpty()) prefix+='/';prefix+=part;
            const QString key=prefix.normalized(QString::NormalizationForm_C).toCaseFolded();
            const auto previous=spellings.find(key);
            if(previous!=spellings.end()&&previous->second!=prefix) {error="代码目录大小写不一致："+source.toStdString();return false;}
            spellings.emplace(key,prefix);
        }
        sources.push_back(QDir(assets).filePath(relative));
        if(suffix=="hpp") headers.push_back(QDir(assets).filePath(relative));
    }
    sources.sort();headers.sort();return true;
}
}

bool CodeEditorXcodeBackend::Generate(const CodeEditorProjectRequest& request,CodeEditorProjectResult& result,
    std::string& error,const std::function<bool()>& cancelled) const {
    result={};error.clear();
    QString root,assets,directory;QStringList sources,headers;
    if(!Paths(request,root,assets,directory,sources,headers,error)) return false;
    if(!ValidateGeneratedTree(directory,error)) return false;
    QLockFile lock(QDir(directory).filePath(".project.lock"));lock.setStaleLockTime(0);
    if(!lock.tryLock(0)) {error="另一个进程正在生成当前项目的代码工程。";return false;}
    if(cancelled&&cancelled()) {error="代码工程操作已取消。";return false;}
    const QString sdk=QFileInfo(request.RuntimeSDKDirectory).canonicalFilePath();
    QStringList sdkSources;
    QDirIterator sdkFiles(sdk,QStringList{"*.h","*.hpp","*.inl"},QDir::Files,QDirIterator::Subdirectories);
    while(sdkFiles.hasNext()) sdkSources.push_back(sdkFiles.next());
    sdkSources.sort();
    QByteArray index="#include \"RuntimeScriptModule.h\"\n";
    for(QString header:headers) {
        header.replace('\\',"\\\\");header.replace('"',"\\\"");
        index+="#include \""+header.toUtf8()+"\"\n";
    }
    const QString configuration=request.Configuration.isEmpty()?QStringLiteral("Debug"):request.Configuration;
    if(!QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$")).match(configuration).hasMatch()) {
        error="当前代码编译配置名称无效。";return false;
    }
    // The external build uses the current ScriptCompiler kit. Do not expose
    // additional Xcode configurations that would still compile this same kit.
    QString cmake="cmake_minimum_required(VERSION 3.21)\nset(CMAKE_CONFIGURATION_TYPES "+CMakeString(configuration)+" CACHE STRING \"GameSprout compiler configuration\" FORCE)\nproject(GameLogic LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_CXX_EXTENSIONS OFF)\n";
    cmake+="set(CMAKE_CXX_FLAGS "+CMakeString(request.BaseFlags)+")\n";
    cmake+="set(CMAKE_CXX_FLAGS_"+configuration.toUpper()+' '+CMakeString(request.ConfigurationFlags)+")\n";
    cmake+="set(game_sources\n";
    for(const QString& source:sources) cmake+="  "+CMakeString(source)+'\n';
    cmake+=")\nset(runtime_sdk_headers\n";
    for(const QString& header:sdkSources) cmake+="  "+CMakeString(header)+'\n';
    cmake+=")\n";
    cmake+="add_custom_target(GameLogic ALL\n  COMMAND "+CMakeString(request.BuildToolProgram);
    for(const QString& argument:request.BuildToolArguments) {
        if(!SafeText(argument)) {error="代码编译命令参数无效。";return false;}
        cmake+=' '+CMakeString(argument);
    }
    cmake+="\n  WORKING_DIRECTORY "+CMakeString(root)+"\n  SOURCES ${game_sources} ${runtime_sdk_headers}\n  COMMENT \"Build GameLogic with the GameSprout reflection and native compilation pipeline\"\n  VERBATIM)\n";
    cmake+="set_target_properties(GameLogic PROPERTIES XCODE_GENERATE_SCHEME TRUE XCODE_SCHEME_LAUNCH_CONFIGURATION "+CMakeString(configuration)+" XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING NO)\n";
    // Only GameLogic invokes the real pipeline. This target provides C++20
    // indexing context for actual Assets sources, never a second runtime DLL.
    cmake+="add_library(GameLogicIndex OBJECT GameLogic.index.cpp ${game_sources} ${runtime_sdk_headers})\n";
    cmake+="target_include_directories(GameLogicIndex PRIVATE "+CMakeString(sdk)+' '+CMakeString(assets)+")\n";
    cmake+="set_target_properties(GameLogicIndex PROPERTIES EXCLUDE_FROM_ALL TRUE XCODE_GENERATE_SCHEME FALSE XCODE_ATTRIBUTE_SKIP_INSTALL YES)\n";
    cmake+="source_group(TREE "+CMakeString(assets)+" PREFIX Assets FILES ${game_sources})\n";
    cmake+="source_group(TREE "+CMakeString(sdk)+" PREFIX \"Runtime SDK\" FILES ${runtime_sdk_headers})\n";
    cmake+="source_group(Generated FILES GameLogic.index.cpp)\n";
    const QString project=QDir(directory).filePath("GameLogic.xcodeproj");
    const QString projectFile=QDir(project).filePath("project.pbxproj");
    const QString stateFile=QDir(directory).filePath("CodeEditorProject.json");
    QCryptographicHash signature(QCryptographicHash::Sha256);
    signature.addData(QByteArray("CodeEditorXcode/1\n"));signature.addData(cmake.toUtf8());signature.addData(index);
    for(const QString& argument:{request.CMakeProgram,request.CompilerProgram,request.Architectures,request.Sysroot,request.DeploymentTarget}) signature.addData(argument.toUtf8()+'\n');
    const QString digest=QString::fromLatin1(signature.result().toHex());
    const QJsonObject record=QJsonDocument::fromJson(Read(stateFile)).object();
    const QString projectHash=QString::fromLatin1(QCryptographicHash::hash(Read(projectFile),QCryptographicHash::Sha256).toHex());
    bool changed=false;
    if(!Write(QDir(directory).filePath("CMakeLists.txt"),cmake.toUtf8(),changed,error)||
       !Write(QDir(directory).filePath("GameLogic.index.cpp"),index,changed,error)) return false;
    result.ProjectPath=project;
    if(!changed&&record.value("Version").toInt()==1&&record.value("Signature").toString()==digest&&
       QFileInfo(projectFile).isFile()&&record.value("ProjectHash").toString()==projectHash) return true;
    if(!ResetRelocatedCache(directory,error)) return false;
    QStringList arguments={"-S",directory,"-B",directory,"-G","Xcode","-DCMAKE_CXX_COMPILER="+request.CompilerProgram};
    if(!request.Architectures.isEmpty()) arguments<<"-DCMAKE_OSX_ARCHITECTURES="+request.Architectures;
    if(!request.Sysroot.isEmpty()) arguments<<"-DCMAKE_OSX_SYSROOT="+request.Sysroot;
    if(!request.DeploymentTarget.isEmpty()) arguments<<"-DCMAKE_OSX_DEPLOYMENT_TARGET="+request.DeploymentTarget;
    if(!Run(request.CMakeProgram,arguments,directory,result.Diagnostics,error,cancelled)) return false;
    if(!QFileInfo(projectFile).isFile()) {error="CMake 没有生成 GameLogic.xcodeproj。";return false;}
    const QString generatedHash=QString::fromLatin1(QCryptographicHash::hash(Read(projectFile),QCryptographicHash::Sha256).toHex());
    const QJsonObject saved{{"Version",1},{"Signature",digest},{"ProjectHash",generatedHash}};
    if(!Write(stateFile,QJsonDocument(saved).toJson(QJsonDocument::Indented),changed,error)) return false;
    result.Changed=true;return true;
}
QStringList CodeEditorXcodeBackend::OpenArguments(const QString& project,const QString& source,int line) {
    QStringList arguments={"xed","--project",project};
    if(line>0) arguments<<"--line"<<QString::number(line);
    arguments<<source;return arguments;
}
bool CodeEditorXcodeBackend::Open(const QString& project,const QString& source,int line,std::string& error,
    const std::function<bool()>& cancelled) const {
    error.clear();
    if(!QFileInfo(QDir(project).filePath("project.pbxproj")).isFile()||!QFileInfo(source).isFile()||
       !QDir::isAbsolutePath(project)||!QDir::isAbsolutePath(source)) {error="无法打开不存在的 Xcode 工程或源文件。";return false;}
    QString diagnostics;
    return Run("/usr/bin/xcrun",OpenArguments(project,source,line),QFileInfo(project).absolutePath(),diagnostics,error,cancelled);
}
#endif

#include "ScriptCompiler.h"
#include "ScriptToolchain.h"
#include "ScriptBindingsGenerator.h"
#include "ScriptBuildStorage.h"
#include "ScriptBuildProcess.h"
#include "ScriptSchemaDocument.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QThread>
#include <map>

namespace {
bool Write(const QString& path,const QByteArray& data,std::string& error) {
    return ScriptBuildStorage::WriteIfChanged(path,data,error);
}
QString CMakeString(QString value) {
    value.replace('\\',"\\\\");value.replace('"',"\\\"");value.replace('$',"\\$");return '"'+value+'"';
}
QString Quoted(QString value) { value.replace('\\', '/'); value.replace(';', "\\;"); value.replace('$', "\\$"); value.replace('"', "\\\""); return '"' + value + '"'; }
bool Run(const QStringList& args,const QString& directory,ScriptCompileResult& result,
         const std::function<bool()>& cancelled) {
    ScriptBuildProcess::Result process;
    const bool success=ScriptBuildProcess::Run(QString::fromUtf8(GAMESPROUT_SCRIPT_CMAKE),args,directory,true,16*1024*1024,process,cancelled);
    result.Diagnostics+=process.Output.right(1024*1024).toStdString();
    if(!process.Error.isEmpty()) result.Diagnostics+='\n'+process.Error.toStdString();
    return success;
}
bool ValidatePath(const QString& path,std::set<QString>& files,std::map<QString,QString>& spellings,std::string& error) {
    const QStringList parts=path.split('/');
    static const std::set<QString> reserved={"cmakelists.txt","gamelogic.generated.cpp","gamelogic.reflection.cpp","gamelogic.buildstamp.cpp"};
    if(parts.isEmpty() || reserved.contains(parts.first().toCaseFolded())) {error="Script path uses a reserved generated Sources name: "+path.toStdString();return false;}
    const QString key=path.normalized(QString::NormalizationForm_C).toCaseFolded();
    if(!files.insert(key).second) {error="Duplicate normalized/case-insensitive script path: "+path.toStdString();return false;}
    QString prefix;
    for(const QString& part:parts) {
        if(!prefix.isEmpty()) prefix+='/';prefix+=part;
        const QString folded=prefix.normalized(QString::NormalizationForm_C).toCaseFolded();
        const auto previous=spellings.find(folded);
        if(previous!=spellings.end() && previous->second!=prefix) {error="Inconsistent script path casing: "+previous->second.toStdString()+" / "+prefix.toStdString();return false;}
        spellings.emplace(folded,prefix);
    }
    return true;
}
}

ScriptCompilerToolchain ScriptCompiler::GetToolchain() const {
    return {GAMESPROUT_SCRIPT_SDK,GAMESPROUT_SCRIPT_CMAKE,GAMESPROUT_SCRIPT_COMPILER,
        GAMESPROUT_SCRIPT_CONFIGURATION,GAMESPROUT_SCRIPT_ARCH,GAMESPROUT_SCRIPT_SYSROOT,
        GAMESPROUT_SCRIPT_DEPLOYMENT,GAMESPROUT_SCRIPT_BASE_FLAGS,GAMESPROUT_SCRIPT_CONFIGURATION_FLAGS};
}

bool ScriptCompiler::Compile(const ScriptCompileRequest& request,ScriptCompileResult& result,
                             const std::function<bool()>& cancelled) const {
    result={};
    const QDir assets(QString::fromStdString(request.AssetsRoot));
    const QString root=QFileInfo(assets.absolutePath()).canonicalFilePath();
    const QString requestedBuild=QDir::cleanPath(QString::fromStdString(request.BuildRoot));
    if(root.isEmpty() || request.BuildRoot.empty() || QFileInfo(requestedBuild).isSymLink() || !QDir().mkpath(requestedBuild)) {result.Diagnostics="Assets/build directory is invalid";return false;}
    const QString directory=QFileInfo(requestedBuild).canonicalFilePath();
    QLockFile lock(QDir(directory).filePath(".compile.lock"));lock.setStaleLockTime(0);
    QElapsedTimer lockWait;lockWait.start();
    while(!lock.tryLock(0)) {
        if(cancelled && cancelled()) {result.Diagnostics="Script compilation cancelled while waiting for the GameLogic build lock";return false;}
        if(request.LockWaitMilliseconds<=0 || lockWait.elapsed()>=request.LockWaitMilliseconds) {
            result.Diagnostics="Another process is compiling this project's GameLogic";return false;
        }
        QThread::msleep(25);
    }
    if(cancelled && cancelled()) {result.Diagnostics="Script compilation cancelled";return false;}
    const QString sourceRoot=QDir(directory).filePath("Sources");
    const QString build=QDir(directory).filePath("Build");
    const QString output=QDir(build).filePath("Output");
    if(QFileInfo(sourceRoot).isSymLink() || QFileInfo(build).isSymLink() || QFileInfo(output).isSymLink() || !QDir().mkpath(output)) {result.Diagnostics="Cannot create fixed script build directories";return false;}
    const QString statePath=QDir(build).filePath("ScriptBuildState.json");
    const QString toolchainPath=QDir(build).filePath("CMakeToolchain.json");
    const QString library=QDir(output).filePath(QString::fromUtf8(GAMESPROUT_SCRIPT_PREFIX "GameLogic" GAMESPROUT_SCRIPT_SUFFIX));
    const QString schemaPath=QDir(output).filePath("GameLogic.schema.json");
    QCryptographicHash toolchain(QCryptographicHash::Sha256);
    toolchain.addData(QByteArray("FixedNativeScriptBuild/1/" GAMESPROUT_SCRIPT_COMPILER "/" GAMESPROUT_SCRIPT_CONFIGURATION "/" GAMESPROUT_SCRIPT_SYSROOT "/" GAMESPROUT_SCRIPT_ARCH "/" GAMESPROUT_SCRIPT_SUFFIX "/" GAMESPROUT_SCRIPT_COMPILER_VERSION "/" GAMESPROUT_SCRIPT_COMPILER_TARGET "/" GAMESPROUT_SCRIPT_PROCESSOR "/" GAMESPROUT_SCRIPT_DEPLOYMENT "/" GAMESPROUT_SCRIPT_CRT "/" GAMESPROUT_SCRIPT_GENERATOR "/" GAMESPROUT_SCRIPT_GENERATOR_PLATFORM "/" GAMESPROUT_SCRIPT_GENERATOR_TOOLSET "/" GAMESPROUT_SCRIPT_BASE_FLAGS "/" GAMESPROUT_SCRIPT_CONFIGURATION_FLAGS "/" GAMESPROUT_SCRIPT_CMAKE "/" GAMESPROUT_SCRIPT_MAKE "/" GAMESPROUT_SCRIPT_SDK));
    // Diagnostic policy participates in both source and toolchain identity;
    // previously accepted code must not bypass new checks via a cached DLL.
    toolchain.addData(QByteArray("ScriptDeclarationSafety/1/-Werror=shadow-field/-Werror=overloaded-virtual"));
    toolchain.addData(directory.toUtf8()); // Relocated CMake trees cannot retain absolute object paths.
    const QString toolchainHash=QString::fromLatin1(toolchain.result().toHex());
    QCryptographicHash hash(QCryptographicHash::Sha256);hash.addData(QByteArray("FixedNativeScriptInputs/3/"));hash.addData(toolchainHash.toUtf8());
    QDirIterator sdk(QString::fromUtf8(GAMESPROUT_SCRIPT_SDK),QStringList{"*.h","*.hpp","*.inl"},QDir::Files,QDirIterator::Subdirectories);
    QStringList sdkFiles;while(sdk.hasNext()) sdkFiles.push_back(sdk.next());sdkFiles.sort();
    for(const QString& path:sdkFiles) {
        if(cancelled && cancelled()) {result.Diagnostics="Script compilation cancelled";return false;}
        QFile file(path);if(!file.open(QIODevice::ReadOnly)) {result.Diagnostics="Cannot read Runtime SDK: "+path.toStdString();return false;}
        hash.addData(path.toUtf8());hash.addData(file.readAll());
    }
    std::vector<ScriptBuildStorage::SourceSnapshot> sources;
    ScriptCompileRequest normalized=request;normalized.Sources.clear();
    auto sorted=request.Sources;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.Path<b.Path;});
    std::set<QString> fileNames;std::map<QString,QString> spellings;
    for(const auto& source:sorted) {
        if(cancelled && cancelled()) {result.Diagnostics="Script compilation cancelled";return false;}
        const QString relative=QDir::cleanPath(QString::fromStdString(source.Path));
        const QString path=QFileInfo(assets.filePath(relative)).canonicalFilePath();
        if(QDir::isAbsolutePath(relative) || relative==".." || relative.startsWith("../") || path.isEmpty() || !path.startsWith(root+'/')) {result.Diagnostics="Script escapes Assets or is missing: "+source.Path;return false;}
        if(!ValidatePath(relative,fileNames,spellings,result.Diagnostics)) return false;
        QFile file(path);if(!file.open(QIODevice::ReadOnly)) {result.Diagnostics="Cannot read script: "+source.Path;return false;}
        const QByteArray bytes=file.readAll();
        hash.addData(relative.toUtf8());hash.addData(QByteArray::fromStdString(source.SourceGuid));hash.addData(bytes);
        const QString extension=QFileInfo(relative).suffix().toLower();
        sources.push_back({relative,bytes,extension=="cpp" || extension=="cc" || extension=="cxx",extension=="hpp"});
        normalized.Sources.push_back({relative.toStdString(),source.SourceGuid});
    }
    for(const auto& source:sources) {
        const QStringList parts=source.Relative.split('/');QString prefix;
        for(qsizetype i=0;i+1<parts.size();++i) {if(!prefix.isEmpty()) prefix+='/';prefix+=parts[i];if(fileNames.contains(prefix.normalized(QString::NormalizationForm_C).toCaseFolded())) {result.Diagnostics="A script path is both a file and a directory: "+prefix.toStdString();return false;}}
    }
    result.Fingerprint=hash.result().toHex().toStdString();
    if(!ScriptBuildStorage::Synchronize(sourceRoot,sources,result.Diagnostics)) return false;
    const QJsonObject state=ScriptBuildStorage::ReadRecord(statePath);
    const QJsonObject configured=ScriptBuildStorage::ReadRecord(toolchainPath);
    const bool sameToolchain=configured.value("Version").toInt()==1 && configured.value("Fingerprint").toString()==toolchainHash;
    std::string hashError;QString currentLibraryHash;
    if(QFileInfo(library).isFile()) currentLibraryHash=ScriptBuildStorage::FileHash(library,hashError);
    const bool sameLibrary=state.value("Version").toInt()==1 && !currentLibraryHash.isEmpty() && state.value("LibraryHash").toString()==currentLibraryHash;
    QFile schemaFile(schemaPath);const QByteArray schemaBytes=schemaFile.open(QIODevice::ReadOnly)?schemaFile.readAll():QByteArray();
    const QString schemaHash=QString::fromLatin1(QCryptographicHash::hash(schemaBytes,QCryptographicHash::Sha256).toHex());
    std::string schemaError;
    const bool sameSchema=!schemaBytes.isEmpty() && state.value("SchemaHash").toString()==schemaHash &&
        ScriptSchemaDocument::Parse(schemaBytes,result.Fingerprint,result.Types,schemaError);
    if(sameToolchain && sameLibrary && sameSchema && state.value("ToolchainFingerprint").toString()==toolchainHash && state.value("Fingerprint").toString()==QString::fromStdString(result.Fingerprint) && ScriptBuildStorage::SnapshotMatches(sourceRoot,state.value("Snapshot").toObject())) {
        result.ReflectionWarnings=state.value("ReflectionWarnings").toString().toStdString();result.Diagnostics=result.ReflectionWarnings;
        result.SchemaPath=schemaPath.toStdString();result.SchemaHash=schemaHash.toStdString();
        result.LibraryPath=library.toStdString();result.LibraryHash=currentLibraryHash.toStdString();result.CacheHit=true;return true;
    }
    // A failed linker or external corruption may leave a newer invalid output.
    // Invalidate that managed candidate so Ninja must relink instead of no-op.
    if(QFileInfo::exists(library) && !sameLibrary && !QFile::remove(library)) {result.Diagnostics="Cannot invalidate damaged GameLogic output";return false;}
    QByteArray aggregate="#include \"Internal/RuntimeScriptBindings.h\"\n";
    QString cmake="cmake_minimum_required(VERSION 3.21)\nproject(GameSproutGameLogic LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 20)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_CXX_EXTENSIONS OFF)\n";
    cmake+="set(CMAKE_CXX_FLAGS "+CMakeString(QString::fromUtf8(GAMESPROUT_SCRIPT_BASE_FLAGS))+")\n";
    cmake+="string(TOUPPER "+Quoted(QString::fromUtf8(GAMESPROUT_SCRIPT_CONFIGURATION))+" script_configuration)\nset(CMAKE_CXX_FLAGS_${script_configuration} "+CMakeString(QString::fromUtf8(GAMESPROUT_SCRIPT_CONFIGURATION_FLAGS))+")\n";
    cmake+="add_library(GameLogic SHARED GameLogic.generated.cpp GameLogic.buildstamp.cpp";
    for (const auto& source : sources) {
        const QString destination=QDir(sourceRoot).filePath(source.Relative);
        if (source.Compile) cmake+=' '+Quoted(destination);
        else if(source.HeaderEntry) {
            QString include=source.Relative; include.replace('\\',"\\\\"); include.replace('"',"\\\"");
            aggregate+="#include \""+include.toUtf8()+"\"\n";
        }
    }
    const QString probe=QDir(sourceRoot).filePath("GameLogic.reflection.cpp");
    if(!Write(probe,aggregate,result.Diagnostics)) return false;
    QByteArray bindings;
    const auto translateDiagnostics=[&] {
        QString diagnostics=QString::fromStdString(result.Diagnostics);
        diagnostics.replace(QFileInfo(sourceRoot).canonicalFilePath(),root);diagnostics.replace(sourceRoot,root);
        result.Diagnostics=diagnostics.toStdString();
    };
    result.Types.clear();
    if(!ScriptBindingsGenerator::Generate(normalized,sourceRoot,probe,bindings,result.Types,result.Diagnostics,result.ReflectionWarnings,cancelled)) {translateDiagnostics();return false;}
    aggregate+=bindings;
    // The build identity lives in the tiny build-stamp TU, keeping helper-only
    // edits incremental while the module and pure schema stay paired.
    aggregate+="extern \"C\" const char* GetGameSproutGameLogicBuildFingerprint() noexcept;\n"
        "extern \"C\" GAMESPROUT_SCRIPT_EXPORT const ::GameSproutRuntime::ScriptModuleAPI* GetGameSproutGameLogic() noexcept { return ::GameSproutRuntime::Internal::GetScriptModuleAPI(GetGameSproutGameLogicBuildFingerprint()); }\n";
    result.Diagnostics=result.ReflectionWarnings;
    cmake+=")\ntarget_include_directories(GameLogic PRIVATE "+Quoted(QString::fromUtf8(GAMESPROUT_SCRIPT_SDK))+' '+Quoted(sourceRoot)+")\n";
    cmake+="if(CMAKE_CXX_COMPILER_ID MATCHES \"^(AppleClang|Clang)$\")\n target_compile_options(GameLogic PRIVATE -Werror=overloaded-virtual -Werror=shadow-field)\nendif()\n";
    cmake+="set_target_properties(GameLogic PROPERTIES RUNTIME_OUTPUT_DIRECTORY "+Quoted(output)+" LIBRARY_OUTPUT_DIRECTORY "+Quoted(output)+" ARCHIVE_OUTPUT_DIRECTORY "+Quoted(output)+" CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)\n";
    cmake+="foreach(script_configuration IN LISTS CMAKE_CONFIGURATION_TYPES)\n string(TOUPPER \"${script_configuration}\" script_upper)\n set_target_properties(GameLogic PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${script_upper} "+Quoted(output)+" LIBRARY_OUTPUT_DIRECTORY_${script_upper} "+Quoted(output)+" ARCHIVE_OUTPUT_DIRECTORY_${script_upper} "+Quoted(output)+")\nendforeach()\n";
    cmake+="include(ProcessorCount)\nProcessorCount(script_jobs)\nif(NOT script_jobs)\n set(script_jobs 1)\nendif()\nset_property(GLOBAL PROPERTY JOB_POOLS script_compile=${script_jobs})\nset_property(TARGET GameLogic PROPERTY JOB_POOL_COMPILE script_compile)\n";
    // Keep the image identity in its own tiny TU: a helper edit need not rebuild
    // the aggregate containing every script class and reflected field bridge.
    const QByteArray buildstamp=QByteArray(
        "#if defined(_WIN32)\n"
        "#define GAMESPROUT_BUILD_STAMP_EXPORT __declspec(dllexport)\n"
        "#else\n"
        "#define GAMESPROUT_BUILD_STAMP_EXPORT __attribute__((visibility(\"default\")))\n"
        "#endif\n"
        "extern \"C\" GAMESPROUT_BUILD_STAMP_EXPORT const char* GetGameSproutGameLogicBuildFingerprint() noexcept {return \""
    )+QByteArray::fromStdString(result.Fingerprint)+"\";}\n";
    if(!Write(QDir(sourceRoot).filePath("GameLogic.buildstamp.cpp"),buildstamp,result.Diagnostics)) return false;
    if(!Write(QDir(sourceRoot).filePath("GameLogic.generated.cpp"),aggregate,result.Diagnostics) || !Write(QDir(sourceRoot).filePath("CMakeLists.txt"),cmake.toUtf8(),result.Diagnostics)) return false;
    if(!sameToolchain && !ScriptBuildStorage::ResetCMakeState(build,result.Diagnostics)) return false;
    QStringList configure{"-S",sourceRoot,"-B",build,"-G",QString::fromUtf8(GAMESPROUT_SCRIPT_GENERATOR),
        "-DCMAKE_CXX_COMPILER="+QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER),
        "-DCMAKE_BUILD_TYPE="+QString::fromUtf8(GAMESPROUT_SCRIPT_CONFIGURATION)};
    if (QFileInfo::exists(QString::fromUtf8(GAMESPROUT_SCRIPT_MAKE))) configure<<"-DCMAKE_MAKE_PROGRAM="+QString::fromUtf8(GAMESPROUT_SCRIPT_MAKE);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_SYSROOT).size()) configure<<"-DCMAKE_OSX_SYSROOT="+QString::fromUtf8(GAMESPROUT_SCRIPT_SYSROOT);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_ARCH).size()) configure<<"-DCMAKE_OSX_ARCHITECTURES="+QString::fromUtf8(GAMESPROUT_SCRIPT_ARCH);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_DEPLOYMENT).size()) configure<<"-DCMAKE_OSX_DEPLOYMENT_TARGET="+QString::fromUtf8(GAMESPROUT_SCRIPT_DEPLOYMENT);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER_TARGET).size()) configure<<"-DCMAKE_CXX_COMPILER_TARGET="+QString::fromUtf8(GAMESPROUT_SCRIPT_COMPILER_TARGET);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_CRT).size()) configure<<"-DCMAKE_MSVC_RUNTIME_LIBRARY="+QString::fromUtf8(GAMESPROUT_SCRIPT_CRT);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_GENERATOR_PLATFORM).size()) configure<<"-A"<<QString::fromUtf8(GAMESPROUT_SCRIPT_GENERATOR_PLATFORM);
    if (QString::fromUtf8(GAMESPROUT_SCRIPT_GENERATOR_TOOLSET).size()) configure<<"-T"<<QString::fromUtf8(GAMESPROUT_SCRIPT_GENERATOR_TOOLSET);
    if(!Run(configure,directory,result,cancelled)) {translateDiagnostics();return false;}
    if(!ScriptBuildStorage::WriteRecord(toolchainPath,{{"Version",1},{"Fingerprint",toolchainHash}},result.Diagnostics)) return false;
    if(!Run({"--build",build,"--config",QString::fromUtf8(GAMESPROUT_SCRIPT_CONFIGURATION),"--parallel",QString::number(std::max(1,QThread::idealThreadCount()))},directory,result,cancelled)) {translateDiagnostics();return false;}
    if(cancelled && cancelled()) {result.Diagnostics="Script compilation cancelled";return false;}
    if(!QFileInfo(library).isFile() || QFileInfo(library).size()==0) {result.Diagnostics+="\nCompiler did not produce GameLogic library";return false;}
    const QString libraryHash=ScriptBuildStorage::FileHash(library,result.Diagnostics);
    if(libraryHash.isEmpty()) return false;
    const QJsonObject snapshot=ScriptBuildStorage::SnapshotHashes(sourceRoot,result.Diagnostics);
    if(snapshot.isEmpty()) return false;
    const QByteArray newSchema=ScriptSchemaDocument::Serialize(result.Fingerprint,result.Types);
    if(!Write(schemaPath,newSchema,result.Diagnostics))return false;
    result.SchemaPath=schemaPath.toStdString();result.SchemaHash=QCryptographicHash::hash(newSchema,QCryptographicHash::Sha256).toHex().toStdString();
    const QJsonObject success{{"Version",1},{"Fingerprint",QString::fromStdString(result.Fingerprint)},{"ToolchainFingerprint",toolchainHash},{"LibraryHash",libraryHash},{"SchemaHash",QString::fromStdString(result.SchemaHash)},{"ReflectionWarnings",QString::fromStdString(result.ReflectionWarnings)},{"Snapshot",snapshot}};
    if(!ScriptBuildStorage::WriteRecord(statePath,success,result.Diagnostics)) return false;
    result.LibraryPath=library.toStdString();result.LibraryHash=libraryHash.toStdString();return true;
}

#include "CodeEditorBackend.h"
#include "CodeEditorXcodeBackend.h"
#include "RuntimeScriptApi.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

static void Check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
static void Write(const QString& path,const QByteArray& bytes) {
    Check(QDir().mkpath(QFileInfo(path).absolutePath()),"Create parent");
    QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Write file");Check(file.write(bytes)==bytes.size(),"Short write");
}
static QByteArray Read(const QString& path) {QFile file(path);Check(file.open(QIODevice::ReadOnly),"Read file");return file.readAll();}
static QByteArray Run(const QStringList& arguments) {
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);process.start("/usr/bin/xcodebuild",arguments);
    Check(process.waitForStarted(5000),"Start xcodebuild");
    Check(process.waitForFinished(60000),"xcodebuild timed out");
    const auto output=process.readAll();Check(process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0,output.toStdString());return output;
}
static void CheckRealPipeline(CodeEditorBackend& backend,const QString& project,const QString& buildTool) {
    Write(project+"/Assets/Hero.hpp",R"(#pragma once
#include "RuntimeScriptModule.h"
class Hero final:public GameSproutRuntime::ScriptBehaviour {
public:
    float Speed=2;
    void Update(GameSproutRuntime::IGameSproutRuntime*,float seconds) override {
        auto value=_GameObject->GetTransform()->GetLocalPosition();value.X+=Speed*seconds;
        _GameObject->GetTransform()->SetLocalPosition(value);
    }
};
GAMESPROUT_SCRIPT(Hero)
)");
    Write(project+"/Assets/Hero.hpp.meta",R"({"Version":1,"ImporterType":"Script","Guid":"0123456789abcdef0123456789abcdef"})");
    Write(project+"/Info/GameLogic.json",R"({"Version":1,"Guid":"1234567890abcdef1234567890abcdef"})");
    CodeEditorProjectRequest request;
    request.ProjectRoot=project;request.RuntimeSDKDirectory=QString::fromUtf8(TEST_RUNTIME_SDK);
    request.CMakeProgram=QString::fromUtf8(TEST_CMAKE_PROGRAM);request.CompilerProgram=QString::fromUtf8(TEST_COMPILER_PROGRAM);
    request.BuildToolProgram=buildTool;request.BuildToolArguments={"--ProjectRoot="+project};
    request.Configuration="Debug";request.ConfigurationFlags="-g";request.Sources={"Hero.hpp"};
    CodeEditorProjectResult result;std::string error;
    Check(backend.Generate(request,result,error,[]{return false;}),error);
    const auto first=Run({"-project",result.ProjectPath,"-scheme","GameLogic","-configuration","Debug","build"});
    Check(first.contains("GameLogic build succeeded"),"Xcode did not complete the real reflection/native pipeline: "+first.toStdString());
    const QString library=project+"/Library/ScriptBuild/Build/Output/libGameLogic.dylib";
    Check(QFileInfo(library).isFile(),"Xcode build did not produce the fixed native DLL");
    const auto timestamp=QFileInfo(library).lastModified().toMSecsSinceEpoch();
    QLibrary module(library);Check(module.load(),module.errorString().toStdString());
    const auto get=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(module.resolve(GameSproutRuntime::ScriptModuleEntryName));
    const auto* api=get?get():nullptr;
    Check(api&&api->Version==GameSproutRuntime::ScriptModuleABIVersion&&api->Count==1&&api->Types[0].Info.Name=="Hero"&&api->Types[0].Fields.size()==1,
        "Xcode product is missing generated script registration or field bridges");
    Check(module.unload(),"Unload real test module");
    const auto cached=Run({"-project",result.ProjectPath,"-scheme","GameLogic","-configuration","Debug","build"});
    Check(cached.contains("GameLogic cache hit")&&QFileInfo(library).lastModified().toMSecsSinceEpoch()==timestamp,"Second Xcode build lost native incremental cache");
    const auto state=QJsonDocument::fromJson(Read(project+"/Library/ScriptBuild/Build/ScriptBuildState.json")).object();
    Check(!state.value("Fingerprint").toString().isEmpty(),"Real build has no successful state record");
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--build-probe") {
        Write(QString::fromLocal8Bit(argv[2]),"GameLogic external pipeline invoked\n");return 0;
    }
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Create temporary project");
        const QString root=temporary.path()+"/项目 with spaces";
        const QString source=root+"/Assets/Code 子目录/Bad Script.hpp";
        Write(source,"#pragma once\nTHIS IS DELIBERATELY BROKEN C++\n");
        Write(root+"/Library/ScriptBuild/Build/CMakeCache.txt","native cache remains untouched\n");
        const QString marker=root+"/build marker.txt";
        CodeEditorProjectRequest request;
        request.ProjectRoot=root;request.RuntimeSDKDirectory=QString::fromUtf8(TEST_RUNTIME_SDK);
        request.CMakeProgram=QString::fromUtf8(TEST_CMAKE_PROGRAM);request.CompilerProgram=QString::fromUtf8(TEST_COMPILER_PROGRAM);
        request.BuildToolProgram=QCoreApplication::applicationFilePath();request.BuildToolArguments={"--build-probe",marker};
        request.Sources={"Code 子目录/Bad Script.hpp"};request.Configuration="Debug";request.ConfigurationFlags="-g";
        CodeEditorBackend* backend=CodeEditorBackend::Create();
        CodeEditorProjectResult first;std::string error;
        Check(backend->Generate(request,first,error,[]{return false;}),error);
        Check(first.Changed&&first.ProjectPath.endsWith("/Library/ScriptBuild/Xcode/GameLogic.xcodeproj"),"Incorrect fixed project path");
        const QString projectFile=first.ProjectPath+"/project.pbxproj";
        const QByteArray project=Read(projectFile);
        const QString xcode=QFileInfo(first.ProjectPath).absolutePath();
        const QByteArray cmake=Read(xcode+"/CMakeLists.txt");
        Check(cmake.contains(QFileInfo(source).canonicalFilePath().toUtf8())&&!cmake.contains("ScriptBuild/Sources"),"Xcode points at a cache mirror");
        Check(cmake.contains("source_group(TREE")&&cmake.contains("PREFIX Assets")&&cmake.contains("PREFIX \"Runtime SDK\""),"Missing directory trees");
        Check(cmake.contains("EXCLUDE_FROM_ALL TRUE"),"Index target participates in normal build");
        Check(project.contains("GameLogicIndex")&&project.contains("Bad Script.hpp"),"Authored files missing in Xcode");
        const qint64 generated=QFileInfo(projectFile).lastModified().toMSecsSinceEpoch();
        CodeEditorProjectResult repeated;
        Check(backend->Generate(request,repeated,error,[]{return false;}),error);
        Check(!repeated.Changed&&Read(projectFile)==project&&QFileInfo(projectFile).lastModified().toMSecsSinceEpoch()==generated,"Unchanged project was rewritten");
        Write(source,"#pragma once\n// Another syntax error may be edited without losing the project\nBAD !!!\n");
        Check(backend->Generate(request,repeated,error,[]{return false;})&&!repeated.Changed,"Source contents forced project regeneration");
        const auto listed=Run({"-list","-project",first.ProjectPath});
        Check(listed.contains("Schemes:")&&listed.contains("GameLogic"),"Missing GameLogic scheme");
        Check(!QString::fromUtf8(listed).section("Schemes:",1).contains("GameLogicIndex"),"Index-only target became a runnable scheme");
        Check(!QString::fromUtf8(listed).section("Build Configurations:",1).section("If no build configuration",0,0).contains("Release"),"Xcode exposed a configuration different from the compiler kit");
        Run({"-project",first.ProjectPath,"-scheme","GameLogic","-configuration","Debug","build"});
        Check(Read(marker)=="GameLogic external pipeline invoked\n","GameLogic did not invoke the provided build tool");
        // Invalid source contents did not make the IDE project unavailable and
        // the main target did not compile the unrelated index-only target.
        const QString added=root+"/Assets/Helpers/Added.cpp";Write(added,"int Added() { return 1; }\n");
        request.Sources.push_back("Helpers/Added.cpp");
        Check(backend->Generate(request,repeated,error,[]{return false;})&&repeated.Changed,"Added source did not update project");
        Check(Read(projectFile).contains("Added.cpp"),"Added source missing");
        request.Sources.removeAll("Code 子目录/Bad Script.hpp");QFile::remove(source);
        Check(backend->Generate(request,repeated,error,[]{return false;})&&repeated.Changed,"Removed source did not update project");
        Check(!Read(projectFile).contains("Bad Script.hpp"),"Deleted source remained in project");
        request.Sources.push_back("../Escape.hpp");
        Check(!backend->Generate(request,repeated,error,[]{return false;}),"Path outside Assets accepted");request.Sources.removeLast();
        request.Sources.push_back("Helpers/Added.cpp");
        Check(!backend->Generate(request,repeated,error,[]{return false;}),"Duplicate source accepted");request.Sources.removeLast();
        Check(!backend->Generate(request,repeated,error,[]{return true;}),"Cancelled generation accepted");
        const auto open=CodeEditorXcodeBackend::OpenArguments(first.ProjectPath,added,23);
        Check(open==QStringList({"xed","--project",first.ProjectPath,"--line","23",added}),"xed argument boundaries or file selection lost");
        Check(!backend->Open(first.ProjectPath,source,0,error,[]{return false;}),"Opening deleted source accepted");
        Check(Read(root+"/Library/ScriptBuild/Build/CMakeCache.txt")=="native cache remains untouched\n","Xcode generation changed the native CMake tree");
        // A relocated project rebuilds only its managed generator cache; authored
        // files and the separately owned native Build tree remain unchanged.
        const QString moved=temporary.path()+"/移动 project";
        Check(QDir().rename(root,moved),"Relocate test project");
        request.ProjectRoot=moved;request.BuildToolArguments={"--build-probe",moved+"/build marker.txt"};
        Check(backend->Generate(request,repeated,error,[]{return false;})&&repeated.Changed,error);
        Check(Read(moved+"/Library/ScriptBuild/Build/CMakeCache.txt")=="native cache remains untouched\n","Relocation discarded the native build tree");
        Check(Read(repeated.ProjectPath+"/project.pbxproj").contains("Added.cpp"),"Relocated project missing current source");
        const QString managedFile=repeated.ProjectPath+"/project.pbxproj";
        const QByteArray managed=Read(managedFile);
        const QString outside=temporary.path()+"/external-file.pbxproj";Write(outside,"outside project bytes\n");
        Check(QFile::remove(managedFile)&&QFile::link(outside,managedFile),"Create cache link fixture");
        Check(!backend->Generate(request,repeated,error,[]{return false;})&&Read(outside)=="outside project bytes\n","Generated pbxproj followed a symbolic link");
        Check(QFile::remove(managedFile),"Remove test link");Write(managedFile,managed);
        if(argc==2&&!QString::fromLocal8Bit(argv[1]).isEmpty())
            CheckRealPipeline(*backend,temporary.path()+"/真实 pipeline 项目",QString::fromLocal8Bit(argv[1]));
        delete backend;
        std::cout<<"PASS real Xcode generation, editable Assets tree, separate build/cache, stable no-op, source add/remove, broken source, external build command, and xed project/file arguments\n";
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}

#include "ScriptCompiler.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <thread>
#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#endif

static void Check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
static void Write(const QString& path,const QByteArray& bytes) {
    QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write fixture");
    Check(file.write(bytes)==bytes.size(),"Short fixture write");
}
static QByteArray Read(const QString& path) {
    QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read fixture");return file.readAll();
}
static void Meta(const QString& path,const char* guid) {
    Write(path+".meta",QJsonDocument(QJsonObject{{"Version",1},{"ImporterType","Script"},{"Guid",guid}}).toJson());
}
struct CommandResult {int Exit=-1;QByteArray Output;};
static CommandResult Run(const QString& executable,const QString& project) {
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(executable,{"--ProjectRoot="+project});
    Check(process.waitForStarted(5000),"Cannot start command tool");
    Check(process.waitForFinished(90000),"Command timed out");
    Check(process.exitStatus()==QProcess::NormalExit,"Command crashed");
    return {process.exitCode(),process.readAll()};
}
#if defined(__unix__) || defined(__APPLE__)
static qint64 Child(qint64 parent) {
    QProcess listing;listing.start("/bin/ps",{"-axo","pid=,ppid=,comm="});
    if(!listing.waitForFinished(2000)) return 0;
    const auto lines=listing.readAllStandardOutput().split('\n');
    for(const auto& line:lines) {
        const auto columns=line.simplified().split(' ');
        if(columns.size()>=3&&columns[1].toLongLong()==parent) return columns[0].toLongLong();
    }
    return 0;
}
#endif
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        Check(argc==2,"Pass command executable");
        const QString executable=QString::fromLocal8Bit(argv[1]);
        QTemporaryDir temporary(QDir::tempPath()+"/GameSprout CLI 空间-XXXXXX");
        Check(temporary.isValid(),"Temporary directory");
        const QString project=temporary.path()+"/项目 $literal (native)";
        Check(QDir().mkpath(project+"/Assets/代码 Folder")&&QDir().mkpath(project+"/Info"),"Create project");
        const QString source=project+"/Assets/代码 Folder/Mover.hpp";
        Write(source,R"(#pragma once
#include "RuntimeScriptModule.h"
class Mover final: public GameSproutRuntime::ScriptBehaviour {
public:
    float Speed=2.5f;
    void Update(GameSproutRuntime::IGameSproutRuntime*,float seconds) override {
        auto position=_GameObject->GetTransform()->GetLocalPosition();position.X+=Speed*seconds;
        _GameObject->GetTransform()->SetLocalPosition(position);
    }
};
GAMESPROUT_SCRIPT(Mover)
)");
        auto result=Run(executable,project);
        Check(result.Exit==3&&result.Output.contains("Info/GameLogic.json"),"Missing assembly was accepted");
        Check(!QFileInfo::exists(project+"/Info/GameLogic.json")&&!QFileInfo::exists(source+".meta"),"CLI created assembly/meta identity");
        Write(project+"/Info/GameLogic.json",QJsonDocument(QJsonObject{{"Version",1},{"Guid","0123456789abcdef0123456789abcdef"}}).toJson());
        result=Run(executable,project);
        Check(result.Exit==3&&result.Output.contains(".meta")&&!QFileInfo::exists(source+".meta"),"Missing source metadata accepted or generated");
        Meta(source,"1234567890abcdef1234567890abcdef");
        const QString module=project+"/Assets/代码 Folder/WorldServices.hpp";
        Write(module,R"(#pragma once
#include "RuntimeScriptModule.h"
namespace Sample {
class WorldServices final : public GameSproutRuntime::IModule {
protected:
    bool Initialize(GameSproutRuntime::IGameSproutRuntime*) override { return true; }
    bool Launch(GameSproutRuntime::IGameSproutRuntime*) override { return true; }
};
GAMESPROUT_REGIST_MODULE(WorldServices)
}
)");
        Meta(module,"2234567890abcdef1234567890abcdef");
        const auto metadata=Read(source+".meta"),assembly=Read(project+"/Info/GameLogic.json");
        result=Run(executable,project);Check(result.Exit==0,"Native CLI build failed: "+result.Output.toStdString());
        Check(result.Output.contains("GameLogic build succeeded")&&result.Output.contains("/Build/Output/"),"CLI did not use current fixed native build");
        const QString snapshot=project+"/Library/ScriptBuild/Sources/代码 Folder/Mover.hpp";
        Check(Read(snapshot)==Read(source),"Native compiler did not take real Assets source snapshot");
        const QString schemaPath=project+"/Library/ScriptBuild/Build/Output/GameLogic.schema.json";
        const auto schema=QJsonDocument::fromJson(Read(schemaPath)).object();
        const auto modules=schema.value("Modules").toArray();
        Check(schema.value("Types").toArray().size()==1&&modules.size()==1&&
              modules[0].toObject()==QJsonObject{{"Name","Sample::WorldServices"},{"SourceGuid","2234567890abcdef1234567890abcdef"},
                  {"SourceFile","代码 Folder/WorldServices.hpp"}},"GameLogic schema did not separate module and script registrations");
        result=Run(executable,project);Check(result.Exit==0&&result.Output.contains("GameLogic cache hit"),"Repeated CLI build was not incremental: "+result.Output.toStdString());
        Check(Read(source+".meta")==metadata&&Read(project+"/Info/GameLogic.json")==assembly,"CLI modified asset identity files");

        // Xcode and Editor share one build lock. A waiting IDE build must reuse
        // the completed candidate rather than write concurrently or fail busy.
        QLockFile held(project+"/Library/ScriptBuild/.compile.lock");held.setStaleLockTime(0);
        Check(held.tryLock(0),"Hold compiler lock");
        QProcess waiting;waiting.setProcessChannelMode(QProcess::MergedChannels);
        waiting.start(executable,{"--ProjectRoot="+project});Check(waiting.waitForStarted(5000),"Start waiting CLI");
        QThread::msleep(180);Check(waiting.state()==QProcess::Running,"CLI did not wait for another compiler");
        held.unlock();Check(waiting.waitForFinished(30000)&&waiting.exitCode()==0,"Waiting CLI failed: "+waiting.readAll().toStdString());

#if defined(__unix__) || defined(__APPLE__)
        Check(held.tryLock(0),"Hold cancellation lock");
        QProcess cancelled;cancelled.setProcessChannelMode(QProcess::MergedChannels);
        cancelled.start(executable,{"--ProjectRoot="+project});Check(cancelled.waitForStarted(5000),"Start cancelling CLI");
        QThread::msleep(150);::kill(static_cast<pid_t>(cancelled.processId()),SIGTERM);
        Check(cancelled.waitForFinished(5000)&&cancelled.exitStatus()==QProcess::NormalExit&&cancelled.exitCode()==130,
              "CLI did not gracefully cancel its build-lock wait: "+cancelled.readAll().toStdString());
        held.unlock();
#endif

        const auto valid=Read(source);
        Write(source,valid+"\nTHIS_IS_NOT_VALID_CPP !!!\n");
        result=Run(executable,project);
        Check(result.Exit==4&&result.Output.contains("/Assets/代码 Folder/Mover.hpp")&&!result.Output.contains("ScriptBuild/Sources/代码 Folder/Mover.hpp"),
              "Compiler diagnostics were not mapped to editable Assets: "+result.Output.toStdString());
        Write(source,valid);result=Run(executable,project);Check(result.Exit==0,"Cannot recover corrected source");
        const auto validModule=Read(module);
        Write(module,validModule+R"(
class AdditionalScript final : public GameSproutRuntime::ScriptBehaviour {};
GAMESPROUT_SCRIPT(AdditionalScript)
)");
        result=Run(executable,project);
        Check(result.Exit==4&&result.Output.contains("one gameplay registration marker"),"Conflicting script and module markers were accepted: "+result.Output.toStdString());
        Write(module,validModule);
        const QString helper=project+"/Assets/Other.cpp";Write(helper,"int helper() { return 1; }\n");Meta(helper,"1234567890abcdef1234567890abcdef");
        result=Run(executable,project);Check(result.Exit==3&&result.Output.contains("Duplicate"),"Duplicate source GUID accepted");
        QFile::remove(helper+".meta");QFile::remove(helper);
#if defined(__unix__) || defined(__APPLE__)
        // Observe a real Clang child before interrupting the build. The existing
        // compiler process runner must retire its process tree before CLI exit.
        const QString cancellation=temporary.path()+"/Cancel active compilation";
        Check(QDir().mkpath(cancellation+"/Assets")&&QDir().mkpath(cancellation+"/Info"),"Cancellation project");
        Write(cancellation+"/Info/GameLogic.json",assembly);
        for(int index=0;index<35;++index) {
            const auto name="Actor"+QString::number(index);
            const auto filename=cancellation+"/Assets/"+name+".hpp";
            Write(filename,("#pragma once\n#include \"RuntimeScriptModule.h\"\nclass "+name+
                " final : public GameSproutRuntime::ScriptBehaviour {public: float Speed=1;};\nGAMESPROUT_SCRIPT("+name+")\n").toUtf8());
            Meta(filename,QString::number(index+1,16).rightJustified(32,'0').toLatin1().constData());
        }
        QProcess active;active.setProcessChannelMode(QProcess::MergedChannels);
        active.start(executable,{"--ProjectRoot="+cancellation});Check(active.waitForStarted(5000),"Start active cancellation");
        QElapsedTimer observation;observation.start();qint64 child=0;
        while(active.state()==QProcess::Running&&observation.elapsed()<15000) {
            child=Child(active.processId());if(child) break;
            active.waitForFinished(10);
        }
        Check(child,"No compiler child observed during cancellation fixture");
        ::kill(static_cast<pid_t>(active.processId()),SIGTERM);
        Check(active.waitForFinished(5000)&&active.exitStatus()==QProcess::NormalExit&&active.exitCode()==130,
              "Active compile cancellation failed: "+active.readAll().toStdString());
        errno=0;
        Check(::kill(-static_cast<pid_t>(child),0)==-1&&errno==ESRCH,"A compiler process group survived CLI cancellation");
        Check(!QFileInfo::exists(cancellation+"/Library/ScriptBuild/.compile.lock"),"Cancelled CLI retained the compiler lock");
        const auto probe=cancellation+"/Library/ScriptBuild/Sources/GameLogic.reflection.cpp";
        const auto before=Read(probe);QThread::msleep(350);
        Check(Read(probe)==before,"A compiler child continued writing after CLI exit");
#endif
        Check(QFile::remove(source)&&QFile::remove(source+".meta"),"Cannot switch to module-only project");
        result=Run(executable,project);Check(result.Exit==0,"Module-only GameLogic failed: "+result.Output.toStdString());
        const auto moduleOnly=QJsonDocument::fromJson(Read(schemaPath)).object();
        Check(moduleOnly.value("Types").toArray().isEmpty()&&moduleOnly.value("Modules").toArray()==modules,"Module-only GameLogic schema is wrong");
        result=Run(executable,project);Check(result.Exit==0&&result.Output.contains("GameLogic cache hit"),"Module-only GameLogic was not cached");
        const auto settings=ScriptCompiler().GetToolchain();
        Check(!settings.RuntimeSDKDirectory.empty()&&!settings.CMakeProgram.empty()&&!settings.CompilerProgram.empty()&&!settings.Configuration.empty(),"Public toolchain snapshot is incomplete");
        QProcess usage;usage.start(executable,{"--ProjectRoot",project});Check(usage.waitForFinished(5000)&&usage.exitCode()==2,"Invalid CLI argument rule");
        std::cout<<"PASS actual CLI native build/cache, Unicode/spaces, identity ownership, shared lock wait/cancel and original-source diagnostics\n";
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}

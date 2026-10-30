#pragma once
#include "IAssetsManager.h"
#include "IAssetsPipeline.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include "IConsole.h"
#include "IIDEWindow.h"
#include "FoundationExport.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QDataStream>
#include <QProcess>
#include <QStandardPaths>
#include <cmath>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace GameSprout {
// Exercise the same protected lifecycle access used by the production editor.
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule * module,GameSproutApi * api) {module->BeforeInitialize(api);}
    static bool Initialize(IGameSproutModule * module,GameSproutApi * api) {return module->Initialize(api)&&module->TriggerInitialized(api);}
    static bool Launch(IGameSproutModule * module,GameSproutApi * api) {return module->Launch(api)&&module->TriggerLaunched(api);}
    static void Finished(IGameSproutModule * module,GameSproutApi * api) {module->LaunchFinished(api);}
    static void Update(IGameSproutModule * module,GameSproutApi * api) {module->Update(api);}
    static void Release(IGameSproutModule * module,GameSproutApi * api) {module->Release(api);}
    static void Before(IGameSproutView * view,GameSproutEditor * editor) {view->BeforeInitialize(editor);}
    static bool Initialize(IGameSproutView * view,GameSproutEditor * editor) {return view->Initialize(editor)&&view->TriggerInitialized(editor);}
    static bool Launch(IGameSproutView * view,GameSproutEditor * editor) {return view->Launch(editor)&&view->TriggerLaunched(editor);}
    static void Update(IGameSproutView * view,GameSproutEditor * editor) {view->Update(editor);}
    static void Release(IGameSproutView * view,GameSproutEditor * editor) {view->Release(editor);}
};
}

void Check(bool condition,const std::string & message) {if (!condition) throw std::runtime_error(message);}
class TestProject final : public IProject {
public:
    std::string Root;
    const std::string & GetProjectRoot() const override {return Root;}
    bool Initialize(GameSprout::GameSproutApi *) override {return true;}
    bool Launch(GameSprout::GameSproutApi *) override {return true;}
    void Release(GameSprout::GameSproutApi *) override {}
};
class TestConsole final : public IConsole {
public:
    std::vector<std::string> Errors;
    void ConsoleLog(const char *,int,eConsoleLog level,const std::string & message) override {
        if (qEnvironmentVariableIsSet("GAMESPROUT_TEST_VERBOSE")) std::cerr << message << "\n";
        if (level==eConsoleLog::Error) {Errors.push_back(message);std::cerr<<message<<"\n";}
    }
    bool Initialize(GameSprout::GameSproutApi *) override {return true;}
    bool Launch(GameSprout::GameSproutApi *) override {return true;}
    void Release(GameSprout::GameSproutApi *) override {}
};
class TestIDE final : public IIDEWindow {
public:
    const std::thread::id Main=std::this_thread::get_id();
    int Count=0,Loads=0,Unloads=0;
    void Loading(const std::string &,const std::string &) override {Check(std::this_thread::get_id()==Main,"Loading on worker");++Count;++Loads;}
    void Unloading() override {Check(std::this_thread::get_id()==Main && Count>0,"Invalid Unloading");--Count;++Unloads;}
    void SetLoadingInfo(const std::string &,const std::string &) override {}
    IIDEMenuBar * GetMenuBar() const override {return nullptr;}
    IIDEDocker * CreateDocker(const QString &) override {return nullptr;}
    IIDEDocker * QueryDocker(const QString &) override {return nullptr;}
    bool ResizeDockers(const QList<IIDEDocker *> &,const QList<int> &,Qt::Orientation) override {return false;}
    bool Initialize(GameSprout::GameSproutEditor *) override {return true;}
    bool Launch(GameSprout::GameSproutEditor *) override {return true;}
    void Release(GameSprout::GameSproutEditor *) override {}
};
class TestEditor final : public GameSprout::GameSproutEditor {
public:
    TestEditor():GameSproutEditor(nullptr) {}
    std::map<std::string,GameSprout::IGameSproutModule *> Modules;
    TestIDE * IDE=nullptr;
    GameSprout::IGameSproutModule * FindGameSproutModule(const std::string & name) const override {const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSprout::IGameSproutView * FindGameSproutView(const std::string & name) const override {return name=="IDEWindow"?IDE:nullptr;}
};
class TaskProxy final : public Foundation::ITaskApi {
public:
    Foundation::ITaskApi * Real=nullptr;
    TestIDE * IDE=nullptr;
    bool Reject=false;
    std::function<void()> BeforeCompletion;
    UInt64 RegisterTaskOwner() override {return Real->RegisterTaskOwner();}
    void UnregisterTaskOwner(UInt64 owner) override {Real->UnregisterTaskOwner(owner);}
    bool CancelTask(UInt64 task) override {return Real->CancelTask(task);}
    Foundation::TaskSubmitResult SubmitTask(const Foundation::TaskSubmitOptions & options,Foundation::fTask task,Foundation::fTaskCompleted completed) override {
        Check(IDE->Count>0,"Task submitted before Loading");
        if (Reject) return {};
        return Real->SubmitTask(options,std::move(task),
            [this, completed=std::move(completed)](const Foundation::TaskCompletion & info, bool success, const Foundation::ITaskContext & context) {
                auto hook = std::move(BeforeCompletion);
                BeforeCompletion = {};
                if (hook) hook();
                if (completed) completed(info, success, context);
            });
    }
};
class FoundationProxy final : public Foundation::FoundationApi {
public:
    Foundation::FoundationApi * Real=nullptr;
    TaskProxy Tasks;
    bool Initialize(int argc,char **argv) override {return Real->Initialize(argc,argv);}
    bool Launch() override {return Real->Launch();}
    void Release() override {Real->Release();}
    const char * GetLaunchArg(const std::string & name) const override {return Real->GetLaunchArg(name);}
    void Update() override {Real->Update();}
#define FORWARD_API(Name) Foundation::I##Name * Name() const override {return Real->Name();}
    FORWARD_API(NetApi) FORWARD_API(SshApi) FORWARD_API(SystemApi) FORWARD_API(TimerApi)
    FORWARD_API(HttpApi) FORWARD_API(MemoryApi) FORWARD_API(MysqlApi) FORWARD_API(RedisApi) FORWARD_API(WebSocketsApi)
#undef FORWARD_API
    Foundation::ITaskApi * TaskApi() const override {return const_cast<TaskProxy *>(&Tasks);}
    void RegisterLogEvent(fLogEvent callback) override {Real->RegisterLogEvent(callback);}
    void SetLogTagFunction(fGetLogTagByLevel callback) override {Real->SetLogTagFunction(callback);}
    bool SetLogInfo(const std::string & dir,const std::string & name) override {return Real->SetLogInfo(dir,name);}
    void LogAsync(int level,const char * content,bool console,const char * file,int line) override {Real->LogAsync(level,content,console,file,line);}
    void LogSync(int level,const char * content,bool console,const char * file,int line) override {Real->LogSync(level,content,console,file,line);}
};

GameSprout::IGameSproutModule * LoadModule(const std::string & name) {
#if defined(_WIN32)
    const std::string filename=std::string(TEST_BINARY_ROOT)+"/Logic/Module/"+name+".dll";
    HMODULE module=LoadLibraryW(std::filesystem::path(filename).c_str());
    Check(module!=nullptr,"Cannot load "+filename);
    auto entry=reinterpret_cast<GameSprout::IGameSproutModule *(*)()>(GetProcAddress(module,"GetModule"));
#else
#if defined(__APPLE__)
    const char * suffix=".dylib";
#else
    const char * suffix=".so";
#endif
    const std::string filename=std::string(TEST_BINARY_ROOT)+"/Logic/Module/lib"+name+suffix;
    void * module=dlopen(filename.c_str(),RTLD_NOW|RTLD_LOCAL);
    const char * error=module?nullptr:dlerror();
    Check(module!=nullptr,"Cannot load "+filename+": "+(error?error:""));
    auto entry=reinterpret_cast<GameSprout::IGameSproutModule *(*)()>(dlsym(module,"GetModule"));
#endif
    Check(entry!=nullptr,"Missing module entry");return entry();
}

class TestMeta final : public IAssetMeta {
public:
    TestMeta(eAssetImporterType type,const Guid & guid,std::string path):IAssetMeta(type,guid),Path(std::move(path)) {}
    std::vector<IAsset *> GetSubAssets() const override { return {}; }
    eAssetImportState GetImportState() const override { return eAssetImportState::Pending; }
    std::string GetImportError() const override { return {}; }
    std::string Path;
    Json::Value Settings{Json::objectValue};
    std::string Identities="[]";
    IAssetImportSettings Empty;
    const std::string & GetPath() const override {return Path;}
    IAssetImportSettings * GetImportSettings() const override {return const_cast<IAssetImportSettings *>(&Empty);}
    bool SerializeMeta(std::string & json) const override {
        Json::Value value;value["Guid"]=_Guid.GetGuidString();value["Path"]=Path;value["ImportSettings"]=Settings;
        AssetArtifactIO::Parse(Identities,value["SubAssets"]);json=AssetArtifactIO::JsonText(value);return true;
    }
    bool SetSubAssets(const std::string & json) override {Identities=json;return true;}
    bool SaveMeta() override {return true;}
};

Json::Value ReadArtifact(const std::string & path,QByteArray * payload=nullptr) {
    QFile file(QString::fromStdString(path));Check(file.open(QIODevice::ReadOnly),"Artifact missing");
    QDataStream input(&file);input.setByteOrder(QDataStream::LittleEndian);
    char magic[8];Check(input.readRawData(magic,8)==8 && QByteArray(magic,8)==QByteArray("GSPROUT\0",8),"Artifact magic mismatch");
    quint32 version,length;quint64 size;input>>version>>length>>size;
    Check(version==1 && length<100000000,"Invalid artifact header");
    const QByteArray json=file.read(length);Json::Value value;
    Check(AssetArtifactIO::Parse(json.toStdString(),value),"Artifact JSON invalid");
    const QByteArray data=file.readAll();Check(quint64(data.size())==size,"Artifact payload size mismatch");
    if (payload) *payload=data;return value;
}

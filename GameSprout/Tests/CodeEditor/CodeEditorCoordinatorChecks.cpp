#include "CodeEditor.h"
#include "CodeEditorBackend.h"
#include "IProject.h"
#include "IConsole.h"
#include "IIDEWindow.h"
#include "FoundationExport.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

static void Check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
static void Write(const QString& path,const QByteArray& bytes="// authored source\n"){
    Check(QDir().mkpath(QFileInfo(path).absolutePath()),"Create source parent");
    QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Create source");Check(file.write(bytes)==bytes.size(),"Write source");
}
class FoundationScope {
public:
    explicit FoundationScope(const char* directory){
        Value=Foundation::GetFoundationApi(directory);Check(Value,"Load independent Foundation");
        char name[]="CodeEditorCoordinatorChecks";char* args[]={name,nullptr};
        Check(Value->Initialize(1,args)&&Value->Launch(),"Start real Foundation services");
    }
    ~FoundationScope(){if(Value){Value->Release();delete Value;}}
    Foundation::FoundationApi* Value=nullptr;
};
template<typename Predicate> static void Wait(Foundation::FoundationApi& foundation,Predicate done,const char* message,int milliseconds=5000){
    QElapsedTimer timer;timer.start();
    while(!done()&&timer.elapsed()<milliseconds){foundation.Update();QCoreApplication::processEvents();QThread::msleep(1);}
    Check(done(),message);
}
static void Drain(Foundation::FoundationApi& foundation,int milliseconds=35){
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<milliseconds){foundation.Update();QCoreApplication::processEvents();QThread::msleep(1);}
}
class Project final:public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot()const override{return Root;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Meta final:public IAssetMeta {
public:
    Meta(const char* guid,const std::string& path,eAssetImporterType type=eAssetImporterType::Script):IAssetMeta(type,Guid(guid)),Path(path){}
    std::string Path;
    const std::string& GetPath()const override{return Path;}
    IAssetImportSettings* GetImportSettings()const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets()const override{return {};}
    eAssetImportState GetImportState()const override{return eAssetImportState::NotRequired;}
    std::string GetImportError()const override{return {};}
    bool SerializeMeta(std::string&)const override{return false;}
    bool SetSubAssets(const std::string&)override{return false;}
    bool SaveMeta()override{return false;}
};
class Assets final:public IAssetsManager {
public:
    std::vector<IAssetMeta*> Values;
    std::vector<IAssetListener*> Listeners;
    IAssetMeta* QueryAsset(const Guid& guid)const override{for(auto* value:Values)if(value->_Guid.GetGuidString()==guid.GetGuidString())return value;return nullptr;}
    bool ReimportAsset(const Guid&)override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&)const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&)override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas()const override{return Values;}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&)const override{return {};}
    bool RegisterAssetListener(IAssetListener* listener)override{Listeners.push_back(listener);return true;}
    void UnregisterAssetListener(IAssetListener* listener)override{std::erase(Listeners,listener);}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
    void Event(const Meta& meta,eAssetMetaEvent action,const std::string& old={}){
        const AssetEvent event(action,meta._Guid,meta.Path,old,meta._ImporterType,eAssetImportState::NotRequired,{});
        const auto snapshot=Listeners;
        for(auto* listener:snapshot){if(action==eAssetMetaEvent::Create)listener->OnAssetCreate(event);else if(action==eAssetMetaEvent::Modify)listener->OnAssetModify(event);else listener->OnAssetRemove(event);}
    }
};
class Compiler final:public IScriptCompiler {
public:
    ScriptCompilerToolchain GetToolchain()const override{return {"/SDK/Api","/tools/cmake","/tools/clang++","Debug","arm64","","","-std=c++20","-g"};}
    bool Compile(const ScriptCompileRequest&,ScriptCompileResult&,const std::function<bool()>&)const override{return false;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Console final:public IConsole {
public:
    std::vector<std::string> Errors;
    void ConsoleLog(const char*,int,eConsoleLog level,const std::string& text)override{if(level==eConsoleLog::Error)Errors.push_back(text);}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class IDE final:public IIDEWindow {
public:
    int LoadingCalls=0,UnloadingCalls=0,Outstanding=0;
    IIDEMenuBar* GetMenuBar()const override{return nullptr;}
    IIDEDocker* CreateDocker(const QString&)override{return nullptr;}
    IIDEDocker* QueryDocker(const QString&)override{return nullptr;}
    bool ResizeDockers(const QList<IIDEDocker*>&,const QList<int>&,Qt::Orientation)override{return false;}
    void Loading(const std::string&,const std::string&)override{++LoadingCalls;++Outstanding;}
    void Unloading()override{++UnloadingCalls;--Outstanding;}
    void SetLoadingInfo(const std::string&,const std::string&)override{}
    bool Initialize(GameSproutEditor::GameSproutViewApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*)override{}
};
struct BackendState {
    std::atomic<int> Generated=0,Opened=0,Entered=0,Working=0,Destroyed=0;
    std::atomic<bool> Gate=false,HoldAfterCancel=false,Cancelled=false,FailGenerate=false,FailOpen=false,DestroyedWhileWorking=false;
    std::vector<CodeEditorProjectRequest> Requests;
    std::vector<QString> OpenSources;
    std::thread::id WorkerThread;
};
class Backend final:public CodeEditorBackend {
public:
    explicit Backend(BackendState& state):_State(state){}
    ~Backend()override{if(_State.Working.load())_State.DestroyedWhileWorking=true;++_State.Destroyed;}
    bool Generate(const CodeEditorProjectRequest& request,CodeEditorProjectResult& result,std::string& error,const std::function<bool()>& cancelled)const override{
        BackendState* const state=&_State;
        struct WorkingScope {BackendState& State;WorkingScope(BackendState& value):State(value){++State.Working;}~WorkingScope(){--State.Working;}} working(*state);
        state->WorkerThread=std::this_thread::get_id();state->Requests.push_back(request);
        const int number=++state->Generated;state->Entered.store(number,std::memory_order_release);
        while(state->Gate.load()){
            if(cancelled()){
                state->Cancelled=true;
                while(state->HoldAfterCancel.load())QThread::msleep(1);
                error="cancelled generation";return false;
            }
            QThread::msleep(1);
        }
        if(cancelled()){state->Cancelled=true;error="cancelled generation";return false;}
        if(state->FailGenerate.load()){error="intentional generation failure";return false;}
        result.ProjectPath=QDir(request.ProjectRoot).filePath("Library/ScriptBuild/Xcode/GameLogic.xcodeproj");result.Changed=true;return true;
    }
    bool Open(const QString&,const QString& source,int line,std::string& error,const std::function<bool()>& cancelled)const override{
        Check(line==1,"Source opening line changed");
        _State.OpenSources.push_back(source);++_State.Opened;
        if(cancelled()){error="cancelled opening";return false;}
        if(_State.FailOpen.load()){error="intentional IDE launch failure";return false;}
        return true;
    }
private:
    BackendState& _State;
};
class Listener final:public ICodeEditorListener {
public:
    std::vector<CodeEditorState> States;
    std::thread::id Owner=std::this_thread::get_id();
    bool WrongThread=false;
    void OnCodeEditorStateChanged(const CodeEditorState& state)override{if(Owner!=std::this_thread::get_id())WrongThread=true;States.push_back(state);}
};
class Fixture {
public:
    Fixture(Foundation::FoundationApi& foundation,const QString& root):Foundation(foundation),ProjectRoot(root),A("0123456789abcdef0123456789abcdef","Code/Actor.hpp"),B("1123456789abcdef0123456789abcdef","Code/Other.hpp"),Texture("2123456789abcdef0123456789abcdef","Texture.png",eAssetImporterType::Texture){
        Write(QDir(root).filePath("Assets/"+QString::fromStdString(A.Path)));
        Write(QDir(root).filePath("Assets/"+QString::fromStdString(B.Path)));
        ProjectValue.Root=root.toStdString();AssetsValue.Values={&A,&B,&Texture};
        Value._Project=&ProjectValue;Value._Assets=&AssetsValue;Value._Compiler=&CompilerValue;
        Value._Console=&ConsoleValue;Value._IDE=&IDEValue;Value._Tasks=foundation.TaskApi();
        Value._CreateBackend=[this]{return new Backend(BackendValue);};
        Check(Value.Initialize(nullptr)&&Value.Launch(nullptr),"Start production CodeEditor with real TaskApi");
        Value.LaunchFinished(nullptr);Check(Value.RegisterCodeEditorListener(&ListenerValue),"Subscribe state");
    }
    ~Fixture(){BackendValue.HoldAfterCancel=false;BackendValue.Gate=false;Value.Release(nullptr);Drain(Foundation);}
    void Idle(){Wait(Foundation,[this]{return !Value._Queued&&!Value._Running&&!Value.GetState().Preparing;},"CodeEditor did not become idle");}
    bool Open(const std::string& path){std::string error;return Value.OpenSource(path,error);}
    void StartBlocked(){BackendValue.Gate=true;Check(Open(A.Path),"Queue blocked source");const int expected=BackendValue.Generated+1;
        Wait(Foundation,[&]{return BackendValue.Entered.load(std::memory_order_acquire)>=expected;},"Worker did not enter Generate");}
    Foundation::FoundationApi& Foundation;
    QString ProjectRoot;
    Project ProjectValue;Assets AssetsValue;Compiler CompilerValue;Console ConsoleValue;IDE IDEValue;
    Meta A,B,Texture;
    BackendState BackendValue;
    Listener ListenerValue;
    CodeEditor Value;
};
static void ValidationAndEvents(Foundation::FoundationApi& foundation,const QString& root){
    Fixture test(foundation,root);std::string error;
    Write(QDir(root).filePath("Outside.hpp"));Write(QDir(root).filePath("Assets/Code/Unregistered.hpp"));
    const std::vector<std::string> invalid={"../Outside.hpp",QDir(root).filePath("Assets/Code/Actor.hpp").toStdString(),"Code/Missing.hpp","Code/Unregistered.hpp","Texture.png",std::string("Code/Actor.hpp\0extra",20)};
    for(const auto& path:invalid)Check(!test.Value.OpenSource(path,error)&&!error.empty(),"Accepted invalid source path");
#if defined(__APPLE__) || defined(__linux__)
    const QString link=QDir(root).filePath("Assets/Code/Linked.hpp");
    Check(QFile::link(QDir(root).filePath("Outside.hpp"),link),"Create escaping source symlink");
    Meta linked("4123456789abcdef0123456789abcdef","Code/Linked.hpp");
    test.AssetsValue.Event(linked,eAssetMetaEvent::Create);
    Check(!test.Value.OpenSource(linked.Path,error)&&!error.empty(),"Accepted a source symlink outside Assets");
    test.AssetsValue.Event(linked,eAssetMetaEvent::Remove);Check(QFile::remove(link),"Remove fixture symlink");
#endif
    Check(test.BackendValue.Generated==0&&test.IDEValue.LoadingCalls==0,"Validation submitted background work");
    Check(test.Open(test.A.Path)&&test.Open(test.B.Path),"Queue two source opens");test.Idle();
    Check(test.BackendValue.Generated==1&&test.BackendValue.Opened==1,"Queued double-clicks were not coalesced");
    Check(test.BackendValue.OpenSources.back()==QFileInfo(QDir(root).filePath("Assets/"+QString::fromStdString(test.B.Path))).canonicalFilePath(),"Opened a cache snapshot instead of authored Assets");
    Check(test.BackendValue.WorkerThread!=std::this_thread::get_id()&&!test.ListenerValue.WrongThread,"Generation/callback thread ownership incorrect");
    Check(test.Value.GetState().ProjectPath==QDir(root).filePath("Library/ScriptBuild/Xcode/GameLogic.xcodeproj").toStdString(),"Project location is not fixed");
    const int original=test.BackendValue.Generated;
    Write(QDir(root).filePath("Assets/"+QString::fromStdString(test.A.Path)),"// body changed\n");test.AssetsValue.Event(test.A,eAssetMetaEvent::Modify);Drain(foundation);
    Check(test.BackendValue.Generated==original,"Body-only edit regenerated the IDE project");
    Meta added("3123456789abcdef0123456789abcdef","Nested/新增.hpp");Write(QDir(root).filePath("Assets/"+QString::fromStdString(added.Path)));
    test.AssetsValue.Event(added,eAssetMetaEvent::Create);test.Idle();
    Check(test.BackendValue.Generated==original+1&&test.BackendValue.Opened==1&&test.BackendValue.Requests.back().Sources.contains(QString::fromStdString(added.Path)),"New source did not synchronize without stealing IDE focus");
    const std::string previous=added.Path;const auto previousFile=QDir(root).filePath("Assets/"+QString::fromStdString(previous));
    added.Path="Nested/Renamed.hpp";Check(QFile::rename(previousFile,QDir(root).filePath("Assets/"+QString::fromStdString(added.Path))),"Rename authored source");
    test.AssetsValue.Event(added,eAssetMetaEvent::Modify,previous);test.Idle();
    Check(!test.BackendValue.Requests.back().Sources.contains(QString::fromStdString(previous))&&test.BackendValue.Requests.back().Sources.contains(QString::fromStdString(added.Path)),"Rename left stale project source");
    Check(QFile::remove(QDir(root).filePath("Assets/"+QString::fromStdString(added.Path))),"Delete authored source");test.AssetsValue.Event(added,eAssetMetaEvent::Remove);test.Idle();
    Check(!test.BackendValue.Requests.back().Sources.contains(QString::fromStdString(added.Path)),"Removed source stayed in IDE project");
    test.AssetsValue.Event(test.Texture,eAssetMetaEvent::Modify);Drain(foundation);
    Check(test.BackendValue.Generated==original+3,"Non-native resource regenerated the IDE project");
    Check(test.IDEValue.Outstanding==0&&test.IDEValue.LoadingCalls==test.IDEValue.UnloadingCalls,"Loading did not balance successful jobs");
    Check(test.ConsoleValue.Errors.empty(),"Successful workspace flow logged an error");
}
static void ConcurrentRequests(Foundation::FoundationApi& foundation,const QString& root){
    Fixture test(foundation,root);test.StartBlocked();Check(test.Open(test.B.Path),"Replace active source request");test.BackendValue.Gate=false;test.Idle();
    Check(test.BackendValue.Generated==2&&test.BackendValue.OpenSources.back().endsWith("/Assets/Code/Other.hpp"),"Latest click during preparation was lost");
    test.StartBlocked();
    Check(QFile::remove(QDir(root).filePath("Assets/"+QString::fromStdString(test.A.Path))),"Delete pending source");
    test.AssetsValue.Event(test.A,eAssetMetaEvent::Remove);test.BackendValue.Gate=false;test.Idle();
    Check(!test.BackendValue.Requests.back().Sources.contains(QString::fromStdString(test.A.Path))&&!test.Value._NeedsSync,"Deleted pending source suppressed required structural synchronization");
    Check(!test.Value.GetState().Error.empty()&&!test.ConsoleValue.Errors.empty(),"Deleted pending source error was not reported");
    Check(test.IDEValue.Outstanding==0&&test.IDEValue.LoadingCalls==test.IDEValue.UnloadingCalls,"Loading did not balance concurrent requests");
}
static void FailureAndRelease(Foundation::FoundationApi& foundation,const QString& root){
    Fixture test(foundation,root);test.BackendValue.FailGenerate=true;Check(test.Open(test.A.Path),"Accept failing generation");test.Idle();
    Check(test.Value.GetState().Error=="intentional generation failure"&&test.ConsoleValue.Errors.size()==1,"Generation failure did not publish one state/error");
    test.BackendValue.FailGenerate=false;test.BackendValue.FailOpen=true;Check(test.Open(test.A.Path),"Accept failing launcher");test.Idle();
    Check(test.Value.GetState().Error=="intentional IDE launch failure"&&test.ConsoleValue.Errors.size()==2,"IDE launch failure disappeared");
    test.BackendValue.FailOpen=false;test.StartBlocked();test.BackendValue.HoldAfterCancel=true;
    const auto notifications=test.ListenerValue.States.size();
    const auto destroyedBefore=test.BackendValue.Destroyed.load();
    test.Value.Release(nullptr);
    Wait(foundation,[&]{return test.BackendValue.Cancelled.load();},"Owner release did not cancel real worker");
    const bool deletedEarly=test.BackendValue.Destroyed.load()!=destroyedBefore||test.BackendValue.DestroyedWhileWorking;
    const bool unbalanced=test.IDEValue.Outstanding!=0||test.IDEValue.LoadingCalls!=test.IDEValue.UnloadingCalls;
    const bool retained=!test.AssetsValue.Listeners.empty();
    test.BackendValue.HoldAfterCancel=false;test.BackendValue.Gate=false;
    Wait(foundation,[&]{return test.BackendValue.Working==0&&test.BackendValue.Destroyed>destroyedBefore;},"Worker backend was not released after cancellation");Drain(foundation);
    Check(!deletedEarly,"Module deleted the backend while UnregisterTaskOwner was only requesting cancellation");
    Check(!unbalanced&&!retained,"Release retained an asset listener or Loading reference");
    Check(test.ListenerValue.States.size()==notifications&&test.ConsoleValue.Errors.size()==2,"Cancelled owner invoked a late callback");
    std::string error;
    Check(!test.Value.OpenSource(test.A.Path,error)&&!error.empty(),"Released service accepted a new open");
}
int main(int argc,char** argv){
    QCoreApplication application(argc,argv);
    try{
        Check(argc==2,"Pass independently built Foundation directory");FoundationScope foundation(argv[1]);
        QTemporaryDir temporary(QDir::tempPath()+"/Game Logic 代码-XXXXXX");Check(temporary.isValid(),"Create isolated project");
        ValidationAndEvents(*foundation.Value,QDir(temporary.path()).filePath("Validation"));
        ConcurrentRequests(*foundation.Value,QDir(temporary.path()).filePath("Concurrent"));
        FailureAndRelease(*foundation.Value,QDir(temporary.path()).filePath("Lifecycle"));
        std::cout<<"PASS real Foundation TaskApi: canonical authored paths, coalesced/latest opens, event-only project sync, failures and safe owner cancellation/loading release\n";
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}

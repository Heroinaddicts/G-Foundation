#include "ScriptCompiler.h"
#include "ScriptEditor.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QTemporaryDir>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void Require(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
static QByteArray Read(const QString& path){QFile file(path);Require(file.open(QIODevice::ReadOnly),"Cannot read "+path.toStdString());return file.readAll();}
static void Write(const QString& path,const QByteArray& bytes){QFile file(path);Require(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write "+path.toStdString());Require(file.write(bytes)==bytes.size(),"Short write");}
static qint64 Stamp(const QString& path){QFileInfo file(path);Require(file.isFile(),"Missing file: "+path.toStdString());return file.lastModified().toMSecsSinceEpoch();}
static QByteArray Hash(const QString& path){return QCryptographicHash::hash(Read(path),QCryptographicHash::Sha256).toHex();}
static QString ObjectFile(const ScriptCompileRequest& request,const QString& sourceName){
    QDirIterator files(QString::fromStdString(request.BuildRoot)+"/Build",QDir::Files,QDirIterator::Subdirectories);
    QString result;
    while(files.hasNext()){
        const QString path=files.next(),name=QFileInfo(path).fileName();
        if(name==sourceName+".o"||name==sourceName+".obj") {Require(result.isEmpty(),"Duplicate active object files: "+sourceName.toStdString());result=path;}
    }
    Require(!result.isEmpty(),"Missing CMake object: "+sourceName.toStdString());return result;
}
static QString SlotFile(const ScriptCompileRequest& request,const ScriptCompileResult& result,int slot){
    return QString::fromStdString(request.BuildRoot)+(slot==0?"/Loaded/SlotA/":"/Loaded/SlotB/")+QFileInfo(QString::fromStdString(result.LibraryPath)).fileName();
}
static void CheckFixedWorkspace(const ScriptCompileRequest& request,const ScriptCompileResult& result){
    const QDir root(QString::fromStdString(request.BuildRoot));
    Require(QFileInfo(root.filePath("Sources")).isDir()&&QFileInfo(root.filePath("Build")).isDir(),"Missing fixed Sources/Build roots");
    Require(QFileInfo(QString::fromStdString(result.LibraryPath)).absolutePath()==QDir(root.filePath("Build/Output")).absolutePath(),"Candidate library escaped fixed Build/Output");
    Require(QFileInfo(root.filePath("Build/ScriptBuildState.json")).isFile(),"Missing fixed successful build record");
    Require(!result.LibraryHash.empty()&&Hash(QString::fromStdString(result.LibraryPath)).toStdString()==result.LibraryHash,"Compile result does not identify its exact candidate bytes");
    for(const auto& name:root.entryList(QDir::Dirs|QDir::NoDotAndDotDot))
        Require(name=="Sources"||name=="Build"||name=="Loaded","Unexpected per-build generation directory: "+name.toStdString());
    const QDir loaded(root.filePath("Loaded"));
    const auto slotNames=loaded.entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    Require(slotNames.size()<=2,"Loaded libraries grew past two slots");
    for(const auto& name:slotNames)Require(name=="SlotA"||name=="SlotB","Unexpected loaded generation directory: "+name.toStdString());
}
static void CheckLoadedSlot(ScriptEditor& editor,const ScriptCompileRequest& request,const ScriptCompileResult& result){
    CheckFixedWorkspace(request,result);
    Require(editor._CurrentSlot==0||editor._CurrentSlot==1,"No active library slot");
    Require(editor._SlotLibraries[editor._CurrentSlot]&&editor._SlotLibraries[editor._CurrentSlot]->isLoaded(),"Current slot has no loaded library");
    Require(QFileInfo(SlotFile(request,result,editor._CurrentSlot)).isFile(),"Current slot library file is missing");
}
class Project final:public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot()const override{return Root;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class Guids final:public IGuidCreator {
public:
    Guid CreateGuid()override{return Guid("0123456789abcdef0123456789abcdef");}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};
class LogRecorder final : public GameSproutRuntime::IRuntimeLogListener {
public:
    void OnRuntimeLog(const GameSproutRuntime::RuntimeLogRecord& record) override {
        Records.push_back(record);
    }

    std::vector<GameSproutRuntime::RuntimeLogRecord> Records;
};

static void CheckGameplayLogs(const LogRecorder& recorder, const QString& assetsCode, const QString& mirrorRoot) {
    using GameSproutRuntime::RuntimeLogLevel;
    struct ExpectedLog {
        RuntimeLogLevel Level;
        std::string Message;
        const char* File;
        const char* Format = nullptr;
    };
    const ExpectedLog expected[] = {
        {RuntimeLogLevel::Debug, "模块启动 max 100%", "SessionModule.hpp", "模块启动 %s %d%%"},
        {RuntimeLogLevel::Trace, "脚本启动", "Test.hpp"},
        {RuntimeLogLevel::Warning, "模块更新", "SessionModule.hpp"},
        {RuntimeLogLevel::Debug, "脚本更新", "Test.hpp"},
        {RuntimeLogLevel::Warning, "脚本释放", "Test.hpp"},
        {RuntimeLogLevel::Error, "模块释放 " + std::string(299, '0') + '7',
            "SessionModule.hpp", "模块释放 %0300d"},
    };
    Require(recorder.Records.size() == std::size(expected), "Gameplay lifecycle logs were lost or duplicated");
    for (std::size_t index = 0; index < std::size(expected); ++index) {
        const auto& record = recorder.Records[index];
        const auto& wanted = expected[index];
        Require(record.Level == wanted.Level && record.Message == wanted.Message,
                "Unexpected gameplay log level or UTF-8 message: " + record.Message);
        const QString path = QDir(mirrorRoot).absoluteFilePath(QString::fromStdString(record.File));
        const QString compiledPath = QFileInfo(mirrorRoot + "/Code/" + wanted.File).canonicalFilePath();
        Require(!compiledPath.isEmpty() && QFileInfo(path).canonicalFilePath() == compiledPath,
                "Gameplay log did not report its source header: " + record.File);
        const QByteArray source = Read(assetsCode + '/' + wanted.File);
        const qsizetype message = source.indexOf(wanted.Format ? wanted.Format : wanted.Message.c_str());
        Require(message >= 0 && record.Line == source.left(message).count('\n') + 1,
                "Gameplay log did not report the line of the call: " + record.Message);
    }
}

static EditorScriptType Find(const std::vector<EditorScriptType>& types,const std::string& name){
    for(const auto& type:types)if(type.Type.Name==name)return type;
    throw std::runtime_error("Missing compiled class: "+name);
}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        Require(argc==2,"Pass the independently built Runtime library directory");
        const QString projects=QCoreApplication::applicationDirPath()+"/TestProjects";
        Require(QDir().mkpath(projects),"Test projects directory");
        QTemporaryDir temporary(projects+"/NativeScripts-XXXXXX");Require(temporary.isValid(),"Temp creation");
        temporary.setAutoRemove(false);
        std::cout<<"NativeScripts isolated project: "<<temporary.path().toStdString()<<std::endl;
        Project project;project.Root=temporary.path().toStdString();
        const QString code=temporary.path()+"/Assets/Code";Require(QDir().mkpath(code),"Assets creation");
        Guids guids;ScriptEditor editor;editor._Project=&project;editor._Guids=&guids;
        editor._Runtime=Export(argv[1]);Require(editor._Runtime,"Runtime load");
        std::string path,error;
        Require(editor.CreateScript("Code","Mover",path,error),"Create: "+error);
        Require(path=="Code/Mover.hpp","Relative script path");
        const auto created=Read(code+"/Mover.hpp");
        Require(created.contains("GAMESPROUT_SCRIPT(Mover)")&&!created.contains("ScriptFields")&&!created.contains("::ScriptBehaviour;")&&!created.contains("explicit Mover"),"Created script exposes constructor forwarding or manual binding metadata");
        Require(created.contains("void OnInitialized(")&&!created.contains("void OnInitialize("),"Created script does not use the current OnInitialized lifecycle");
        Require(created.contains("void EarlyUpdate(")&&created.contains("void LateUpdate(")&&!created.contains("void LaterUpdate("),"Created script is missing current Early/Late callbacks");
        Require(!editor.CreateScript("Code","Mover",path,error),"Duplicate source must fail");
        Require(!editor.CreateScript("../","Escaped",path,error),"Escape must fail");
        Require(!editor.CreateScript("Code","class",path,error),"C++ keyword must fail");
        Require(editor.CreateModule("Code","SessionModule",path,error),"Create Module: "+error);
        Require(path=="Code/SessionModule.hpp","Relative module path");
        const auto moduleTemplate=Read(code+"/SessionModule.hpp");
        Require(moduleTemplate.contains("class ISessionModule")&&
            moduleTemplate.contains("GAMESPROUT_REGIST_MODULE(SessionModule)"),"Created module is missing its interface or marker");
        Require(!editor.CreateScript("Code","SessionModule",path,error),"Script creation overwrote a module");
        Write(code+"/SessionModule.hpp",R"(#pragma once
#include "RuntimeScriptModule.h"
class ISessionModule : public GameSproutRuntime::IModule {
public:
    virtual int Ticks() const = 0;
};
class SessionModule final : public ISessionModule {
public:
    int Ticks() const override { return _Ticks; }
protected:
    bool Initialize(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        GS_DEBUG(runtime, "模块启动 %s %d%%", "max", 100);
        return runtime->FindModule<ISessionModule>("SessionModule") == this;
    }
    bool Launch(GameSproutRuntime::IGameSproutRuntime*) override { return true; }
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime, float) override {
        runtime->Log(GameSproutRuntime::RuntimeLogLevel::Warning, "模块更新");
        ++_Ticks;
    }
    void Release(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        GS_ERROR(runtime, "模块释放 %0300d", 7);
    }
private:
    int _Ticks = 0;
};
GAMESPROUT_REGIST_MODULE(SessionModule)
)");
        Write(code+"/Mover.hpp",R"(#pragma once
#include "RuntimeScriptModule.h"
#include <vector>
class Mover final:public GameSproutRuntime::ScriptBehaviour {
public:
    float Speed = 1.0f;
    bool Enabled = true;
    std::int64_t Count = 42;
    std::string Title = "Player";
    GameSproutRuntime::Vector3 Offset = {1,2,3};
    GameSproutRuntime::ResourceReference Texture = {"abcdef0123456789abcdef0123456789",7};
    std::function<void()> RuntimeOnly;
#include "OnlyInsideClass.inl"
    void OnCreate(GameSproutRuntime::IGameSproutRuntime*) override {}
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {auto p=_GameObject->GetTransform()->GetLocalPosition();p.Y=Speed;_GameObject->GetTransform()->SetLocalPosition(p);}
    void EarlyUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void Update(GameSproutRuntime::IGameSproutRuntime*,float delta) override {auto p=_GameObject->GetTransform()->GetLocalPosition();p.X+=Speed*delta*MotionMultiplier;_GameObject->GetTransform()->SetLocalPosition(p);}
    void LateUpdate(GameSproutRuntime::IGameSproutRuntime*,float) override {}
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float delta) override {auto p=_GameObject->GetTransform()->GetLocalPosition();p.Z+=Speed*delta;_GameObject->GetTransform()->SetLocalPosition(p);}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime*) override {}
private:
    static constexpr float MotionMultiplier = 1.0f;
    float Secret = 99;
};
GAMESPROUT_SCRIPT(Mover)
)");
        // A plain user class: five callbacks and one marker, no constructor,
        // persistent ID, ScriptFields method or handwritten reflection code.
        Write(code+"/Test.hpp",R"(#pragma once
#include "RuntimeScriptModule.h"
#include "SessionModule.hpp"
#include <stdexcept>
class Test : public GameSproutRuntime::ScriptBehaviour {
public:
    void OnCreate(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        GS_TRACE(runtime, "脚本启动");
        auto* module=runtime->FindModule<ISessionModule>("SessionModule");
        if(!module||module->Ticks()!=0)throw std::runtime_error("Gameplay module was not ready before script creation");
        if(_GameObject->AddComponent<Test>()!=nullptr)throw std::runtime_error("duplicate self add succeeded");
        if(_GameObject->GetComponent<Test>()!=this)throw std::runtime_error("GetComponent<Test> did not return this");
        auto* mesh=_GameObject->GetComponent<GameSproutRuntime::IMeshComponent>();
        if(!mesh)throw std::runtime_error("GetComponent<IMeshComponent> did not find the host-created mesh");
        if(_GameObject->AddComponent<GameSproutRuntime::IMeshComponent>()!=mesh)throw std::runtime_error("AddComponent<IMeshComponent> did not return the existing builtin");
        GameSproutRuntime::IComponent* component=mesh;
        if(dynamic_cast<GameSproutRuntime::IMeshComponent*>(component)!=mesh)throw std::runtime_error("Builtin interface dynamic_cast failed across the game DLL boundary");
        auto* image=_GameObject->AddComponent<GameSproutRuntime::IImageComponent>();
        if(!image||_GameObject->GetComponent<GameSproutRuntime::IImageComponent>()!=image)throw std::runtime_error("Hidden game DLL cannot create or query a builtin Image");
        if(!_GameObject->RemoveComponent<GameSproutRuntime::IImageComponent>())throw std::runtime_error("Hidden game DLL cannot remove a builtin Image");
    }
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime*) override {auto p=_GameObject->GetTransform()->GetLocalPosition();p.Y=2;_GameObject->GetTransform()->SetLocalPosition(p);}
    void Update(GameSproutRuntime::IGameSproutRuntime* runtime,float delta) override {
        runtime->Log(GameSproutRuntime::RuntimeLogLevel::Debug, "脚本更新");
        const auto* module=runtime->FindModule<ISessionModule>("SessionModule");
        if(!module||module->Ticks()==0)throw std::runtime_error("Module Update did not run before script Update");
        auto p=_GameObject->GetTransform()->GetLocalPosition();p.X+=delta;_GameObject->GetTransform()->SetLocalPosition(p);
    }
    void FixedUpdate(GameSproutRuntime::IGameSproutRuntime*,float delta) override {auto p=_GameObject->GetTransform()->GetLocalPosition();p.Z+=delta;_GameObject->GetTransform()->SetLocalPosition(p);}
    void OnRelease(GameSproutRuntime::IGameSproutRuntime* runtime) override {
        runtime->Log(GameSproutRuntime::RuntimeLogLevel::Warning, "脚本释放");
        if(!runtime->FindModule<ISessionModule>("SessionModule"))throw std::runtime_error("Module released before the script");
        auto p=_GameObject->GetTransform()->GetLocalPosition();p.Y=3;_GameObject->GetTransform()->SetLocalPosition(p);
    }
};
GAMESPROUT_SCRIPT(Test)
)");
        Write(code+"/Helper.cpp","#include \"Mover.hpp\"\nint ScriptHelper(){return 7;}\n");
        Require(QDir().mkpath(code+"/Aux"),"Create independent helper directory");
        Write(code+"/Aux/Independent.cpp","int IndependentHelper(){return 17;}\n");
        Write(code+"/OnlyInsideClass.inl","public:\nint ClassMemberOnly = 0;\n");
        ScriptCompiler compiler;ScriptCompileRequest request;
        request.AssetsRoot=temporary.path().toStdString()+"/Assets";
        request.BuildRoot=temporary.path().toStdString()+"/Library/ScriptBuild";
        request.ArtifactRoot=temporary.path().toStdString()+"/Library/Artifact";
        request.AssemblyGuid="ffeeddccbbaa99887766554433221100";
        request.Sources={{"Code/Mover.hpp","00112233445566778899aabbccddeeff"},
            {"Code/Helper.cpp","11112233445566778899aabbccddeeff"},
            {"Code/OnlyInsideClass.inl","21112233445566778899aabbccddeeff"},
            {"Code/Test.hpp","41112233445566778899aabbccddeeff"},
            {"Code/Aux/Independent.cpp","51112233445566778899aabbccddeeff"},
            {"Code/SessionModule.hpp","71112233445566778899aabbccddeeff"}};
        ScriptCompileResult result;
        Require(compiler.Compile(request,result,[]{return false;}),"Compile: "+result.Diagnostics);
        Require(result.Modules.size()==1&&result.Modules.front().Name=="SessionModule"&&
            result.Modules.front().SourceFile=="Code/SessionModule.hpp"&&
            result.Modules.front().SourceGuid==request.Sources.back().SourceGuid,"Gameplay module metadata");
        CheckFixedWorkspace(request,result);
        const auto outputRepairObject=ObjectFile(request,"Independent.cpp");
        const auto outputRepairStamp=Stamp(outputRepairObject);
        const QByteArray damageMarker="\nNativeScripts deliberately damaged cache tail\n";
        Write(QString::fromStdString(result.LibraryPath),Read(QString::fromStdString(result.LibraryPath))+damageMarker);
        ScriptCompileResult repaired;
        Require(compiler.Compile(request,repaired,[]{return false;}),"Repair corrupted candidate: "+repaired.Diagnostics);
        Require(!repaired.CacheHit&&!Read(QString::fromStdString(repaired.LibraryPath)).endsWith(damageMarker),"Candidate hash mismatch was accepted or Ninja reused damaged output without relinking");
        Require(Stamp(outputRepairObject)==outputRepairStamp,"Output repair rebuilt an unchanged helper instead of relinking");
        result=repaired;CheckFixedWorkspace(request,result);
        Require(result.ReflectionWarnings.find("RuntimeOnly")!=std::string::npos,"Unsupported public field has no reflection warning");
        Require(editor.Load(request,result,error),"Load: "+error);
        Require(editor._Runtime->FindModule("SessionModule")==nullptr,"Loading GameLogic created a gameplay instance in edit mode");
        CheckLoadedSlot(editor,request,result);Require(editor._CurrentSlot==0,"First game module did not use SlotA");
        Require(editor.SetReloadBlocked(true,error),error);
        Require(editor.GetBuildState().ReloadBlocked&&!editor.Load(request,result,error)&&editor._CurrentSlot==0,
            "Execution-session lease did not block even a same-fingerprint candidate load");
        Require(editor.SetReloadBlocked(false,error),error);
        const QString artifact=QString::fromStdString(request.ArtifactRoot+"/"+request.AssemblyGuid+"_1" TEST_NATIVE_LIBRARY_SUFFIX);
        Require(QFileInfo::exists(artifact),"Flat GameLogic artifact");
        const QString mirrorRoot=QString::fromStdString(request.BuildRoot)+"/Sources";
        const QString generated=mirrorRoot+"/GameLogic.generated.cpp";
        const QString independentObject=ObjectFile(request,"Independent.cpp");
        const QString dependentObject=ObjectFile(request,"Helper.cpp");
        const QString generatedObject=ObjectFile(request,"GameLogic.generated.cpp");
        const auto firstIndependentStamp=Stamp(independentObject),firstMirrorStamp=Stamp(mirrorRoot+"/Code/Test.hpp");
        auto types=editor.GetScriptTypes();Require(types.size()==2,"Native class discovery did not deduplicate included headers");
        const auto moverType=Find(types,"Mover"),testType=Find(types,"Test");
        Require(moverType.Type.TypeID==request.Sources[0].SourceGuid&&moverType.Source.LocalID==1,"Automatic stable type identity must come from source .meta");
        Require(moverType.Type.Fields.size()==7&&testType.Type.Fields.empty(),"Automatic public field discovery included private/runtime-only data");
        auto* scene=editor._Runtime->CreateScene("NativeScripts");auto* object=scene->CreateGameObject("Mover");
        auto* script=editor._Runtime->AddScriptComponent(object,moverType.Type.TypeID,moverType.Source,0,error);Require(script,error);
        Require(script->GetFields().at("Mover::Speed").Number==1&&script->GetFields().at("Mover::Count").Integer==42&&script->GetFields().at("Mover::Enabled").Boolean&&
            script->GetFields().at("Mover::Title").String=="Player"&&script->GetFields().at("Mover::Offset").Vector.Z==3&&
            script->GetFields().at("Mover::Texture").Resource.LocalID==7,"Automatically generated field defaults are incorrect");
        auto* plainObject=scene->CreateGameObject("Plain Test");
        Require(plainObject->AddComponent<GameSproutRuntime::IMeshComponent>()!=nullptr,"Host failed to create Mesh for cross-DLL typed lookup");
        auto* plain=editor._Runtime->AddScriptComponent(plainObject,testType.Type.TypeID,testType.Source,0,error);Require(plain,error);
        Require(!editor._Runtime->AddScriptComponent(object,moverType.Type.TypeID,moverType.Source,0,error),"Duplicate script class attached to one object");
        LogRecorder logs;
        Require(editor._Runtime->RegisterLogListener(&logs), "Cannot subscribe to Runtime logs");
        Require(editor._Runtime->BeginExecution(error),error);
        Require(editor._Runtime->FindModule("SessionModule")!=nullptr,"Play did not create the registered module");
        Require(logs.Records.size() == 2, "Startup logs were not delivered before BeginExecution returned");
        editor._Runtime->Update(.016f);
        Require(logs.Records.size() == 4, "Update logs were not delivered before the frame returned");
        Require(plain->GetScriptError().empty()&&plainObject->GetTransform()->GetLocalPosition().Y==2,"Plain Test self-add/Get<this> or lifecycle failed: "+plain->GetScriptError());
        Require(plainObject->GetTransform()->GetLocalPosition().Z==0,"Variable Update called FixedUpdate");
        editor._Runtime->FixedUpdate(.25f);
        Require(plainObject->GetTransform()->GetLocalPosition().Z==.25f,"Explicit fixed step did not invoke plain user callback");
        ScriptCompileResult attemptedReload=result;attemptedReload.Fingerprint+="other";
        const auto executionSlot=editor._CurrentSlot;
        const auto executionArtifact=Read(artifact),executionSlotBytes=Read(SlotFile(request,result,executionSlot));
        Require(!editor.Load(request,attemptedReload,error)&&script->IsScriptTypeAvailable(),"Execution-time reload removed the active module");
        Require(editor._CurrentSlot==executionSlot&&Read(artifact)==executionArtifact&&Read(SlotFile(request,result,executionSlot))==executionSlotBytes,"Rejected execution-time reload modified a slot or published Artifact");
        Require(editor._Runtime->EndExecution(error),error);
        Require(editor._Runtime->FindModule("SessionModule")==nullptr,"Stop retained a live gameplay module");
        Require(plainObject->GetTransform()->GetLocalPosition().Y==3,"Plain user OnRelease was not invoked");
        CheckGameplayLogs(logs, code, mirrorRoot);
        editor._Runtime->UnregisterLogListener(&logs);
        editor._Runtime->Log(GameSproutRuntime::RuntimeLogLevel::Trace, "Detached listener");
        Require(logs.Records.size() == 6, "A detached Runtime listener received a log");
        ScriptCompileResult cached;Require(compiler.Compile(request,cached,[]{return false;})&&cached.CacheHit,"Cache hit");
        Require(Stamp(independentObject)==firstIndependentStamp&&Stamp(mirrorRoot+"/Code/Test.hpp")==firstMirrorStamp,"Identical input rewrote source mirrors or rebuilt a helper object");
        Require(editor.Load(request,cached,error)&&editor._CurrentSlot==executionSlot,"Loading the same fingerprint unnecessarily switched slots");
        GameSproutRuntime::ScriptValue authored;authored.Number=3;Require(script->SetField("Mover::Speed",authored,error),error);
        const auto helperBeforeEdit=Stamp(independentObject),dependentBeforeEdit=Stamp(dependentObject),generatedBeforeEdit=Stamp(generatedObject);
        const auto generatedSourceBeforeEdit=Stamp(generated),moverMirrorBeforeEdit=Stamp(mirrorRoot+"/Code/Mover.hpp");
        Write(code+"/Aux/Independent.cpp","int IndependentHelper(){return 18;}\n");
        ScriptCompileResult helperChanged;Require(compiler.Compile(request,helperChanged,[]{return false;}),"Incremental helper compile: "+helperChanged.Diagnostics);
        Require(!helperChanged.CacheHit&&Stamp(independentObject)!=helperBeforeEdit,"Changed helper translation unit was not rebuilt");
        Require(Stamp(dependentObject)==dependentBeforeEdit&&Stamp(generatedObject)==generatedBeforeEdit&&Stamp(generated)==generatedSourceBeforeEdit&&Stamp(mirrorRoot+"/Code/Mover.hpp")==moverMirrorBeforeEdit,"Independent .cpp edit rebuilt unrelated translation units or rewrote source/aggregate mirrors");
        Require(editor.Load(request,helperChanged,error),error);CheckLoadedSlot(editor,request,helperChanged);
        const auto independentAfterEdit=Stamp(independentObject);
        QByteArray good=Read(code+"/Mover.hpp");good.replace("    float Speed = 1.0f;","    float Speed = 2.0f;\n    float Added = 4.0f;");Write(code+"/Mover.hpp",good);
        const QByteArray oldArtifact=Read(artifact);
        const int beforeInvalidSlot=editor._CurrentSlot;
        const QByteArray beforeInvalidSlotBytes=Read(SlotFile(request,helperChanged,beforeInvalidSlot));
        // Malformed candidates are low-level loader fixtures, not a user API.
        Write(code+"/BadDescriptor.cpp",R"(#include "Internal/RuntimeScriptBindings.h"
class Invalid final:public GameSproutRuntime::ScriptBehaviour {public:float Value=1;};
static const bool BadRegistered=GameSproutRuntime::Internal::RegisterScriptClass<Invalid>("31112233445566778899aabbccddeeff","Invalid",__FILE__,{"31112233445566778899aabbccddeeff",1},{
GameSproutRuntime::Internal::MakeField<Invalid>("Value","A",&Invalid::Value,1.0f),
GameSproutRuntime::Internal::MakeField<Invalid>("Value","B",&Invalid::Value,1.0f)});
)");
        request.Sources.push_back({"Code/BadDescriptor.cpp","31112233445566778899aabbccddeeff"});
        ScriptCompileResult rejected;Require(compiler.Compile(request,rejected,[]{return false;}),"Compile malformed candidate: "+rejected.Diagnostics);
        Require(!editor.Load(request,rejected,error),"Duplicate field descriptors were published; active fields="+std::to_string(script->GetFields().size())+", catalog="+std::to_string(editor.GetScriptTypes().size()));
        Require(script->GetFields().size()==7&&script->GetFields().at("Mover::Speed").Number==3&&Read(artifact)==oldArtifact,"Rejected candidate changed fields or Artifact bytes");
        Require(editor._CurrentSlot==beforeInvalidSlot&&Read(SlotFile(request,rejected,beforeInvalidSlot))==beforeInvalidSlotBytes&&!QFileInfo::exists(SlotFile(request,rejected,1-beforeInvalidSlot)),"Rejected candidate replaced current slot or left its reusable slot occupied");
        Write(code+"/BadDescriptor.cpp",R"(#include "Internal/RuntimeScriptBindings.h"
#include <exception>
class NativeCopyError final:public std::exception {public:~NativeCopyError() override {} const char* what()const noexcept override{return "candidate bridge copy failed";}};
struct CopyThrower {inline static bool Active=false;CopyThrower()=default;CopyThrower(const CopyThrower&){if(Active)throw NativeCopyError();}bool operator()(GameSproutRuntime::IScript&,const GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptReferenceContext&,std::string&)const{return true;}};
class Invalid final:public GameSproutRuntime::ScriptBehaviour {public:float Value=1;};
static const bool BadRegistered=[] {
 auto field=GameSproutRuntime::Internal::MakeField<Invalid>("Value","Value",&Invalid::Value,1.0f);field.Write=CopyThrower{};
 return GameSproutRuntime::Internal::RegisterScriptClass<Invalid>("31112233445566778899aabbccddeeff","Invalid",__FILE__,{"31112233445566778899aabbccddeeff",1},{field});
}();
static const bool TriggerCopyFailure=[] {CopyThrower::Active=true;return true;}();
)");
        ScriptCompileResult throwing;Require(compiler.Compile(request,throwing,[]{return false;}),"Compile throwing candidate: "+throwing.Diagnostics);
        Require(!editor.Load(request,throwing,error)&&!error.empty(),"Candidate with a native-only descriptor absent from its pure schema was accepted: "+error);
        Require(script->GetFields().size()==7&&script->GetFields().at("Mover::Speed").Number==3,"Throwing candidate changed author data");
        Require(editor._CurrentSlot==beforeInvalidSlot&&Read(artifact)==oldArtifact&&!QFileInfo::exists(SlotFile(request,throwing,1-beforeInvalidSlot)),"Throwing candidate damaged slot ownership or Artifact");
        request.Sources.pop_back();Require(QFile::remove(code+"/BadDescriptor.cpp"),"Remove low-level candidate fixture");
        ScriptCompileResult updated;Require(compiler.Compile(request,updated,[]{return false;}),"Compile hot reload: "+updated.Diagnostics);
        Require(Stamp(independentObject)==independentAfterEdit&&Stamp(mirrorRoot+"/Code/Test.hpp")==firstMirrorStamp,"Header-only revisions rebuilt an unrelated helper or rewrote an unchanged source mirror");
        Require(!QFileInfo::exists(mirrorRoot+"/Code/BadDescriptor.cpp"),"Deleted candidate helper remains in Sources");
        Require(editor.Load(request,updated,error),"Atomic hot reload: "+error);
        CheckLoadedSlot(editor,request,updated);
        Require(editor._CurrentSlot==1-beforeInvalidSlot,"Recovered candidate did not reuse the released slot");
        Require(script->GetFields().size()==8&&script->GetFields().at("Mover::Speed").Number==3&&script->GetFields().at("Mover::Added").Number==4,"Hot reload lost author value or new field default");
        QByteArray renamed=Read(code+"/Mover.hpp");renamed.replace("class Mover final","class RenamedMover final").replace("GAMESPROUT_SCRIPT(Mover)","GAMESPROUT_SCRIPT(RenamedMover)");Write(code+"/Mover.hpp",renamed);
        ScriptCompileResult renamedResult;Require(compiler.Compile(request,renamedResult,[]{return false;}),"Compile renamed class: "+renamedResult.Diagnostics);
        Require(editor.Load(request,renamedResult,error),error);
        Require(Find(editor.GetScriptTypes(),"RenamedMover").Type.TypeID==moverType.Type.TypeID&&script->GetFields().at("RenamedMover::Speed").Number==2,"Class rename changed source identity or did not assign new declaring-type field identities");
        Require(script->SetField("RenamedMover::Speed",authored,error),error);
        CheckLoadedSlot(editor,request,renamedResult);
        const auto playVersion=[&](int version) {
            object->GetTransform()->SetLocalPosition({});
            Require(editor._Runtime->BeginExecution(error),error);
            editor._Runtime->Update(.25f);
            const float expected=.75f*version;
            const float actual=object->GetTransform()->GetLocalPosition().X;
            Require(script->GetScriptError().empty()&&actual==expected,"Reused library slot executed stale gameplay code instead of version "+std::to_string(version)+": expected "+std::to_string(expected)+", actual "+std::to_string(actual)+", "+script->GetScriptError());
            Require(editor._Runtime->EndExecution(error),error);
            Require(script->GetFields().at("RenamedMover::Speed").Number==3&&script->GetFields().at("RenamedMover::Added").Number==4,"Code version load changed authored fields");
        };
        int codeVersion=1;
        const auto compileVersion=[&](int version) {
            QByteArray source=Read(code+"/Mover.hpp");
            const QByteArray previous="static constexpr float MotionMultiplier = "+QByteArray::number(codeVersion)+".0f;";
            Require(source.count(previous)==1,"Missing deterministic code version fixture");
            source.replace(previous,"static constexpr float MotionMultiplier = "+QByteArray::number(version)+".0f;");Write(code+"/Mover.hpp",source);
            codeVersion=version;
            ScriptCompileResult candidate;
            Require(compiler.Compile(request,candidate,[]{return false;}),"Compile code version: "+candidate.Diagnostics);
            Require(Stamp(independentObject)==independentAfterEdit&&Stamp(mirrorRoot+"/Code/Test.hpp")==firstMirrorStamp,"Code-only version changes rebuilt unrelated objects or rewrote unchanged mirrors");
            CheckFixedWorkspace(request,candidate);return candidate;
        };
        playVersion(1);
        // A real extra QLibrary lease makes unload fail without mocking dlclose.
        // A retained slot must remain untouched until that lease is released.
        {
            const int heldSlot=editor._CurrentSlot;
            const QString heldPath=SlotFile(request,renamedResult,heldSlot);
            const auto heldBytes=Read(heldPath);
            QLibrary lease(heldPath);Require(lease.load(),"Cannot retain active slot: "+lease.errorString().toStdString());
            const auto leasedEntry=reinterpret_cast<GameSproutRuntime::fGetScriptModule>(lease.resolve(GameSproutRuntime::ScriptModuleEntryName));
            Require(leasedEntry&&leasedEntry()->Count==2,"Cannot call externally leased native module");
            auto secondVersion=compileVersion(2);
            const auto candidateBytes=Read(QString::fromStdString(secondVersion.LibraryPath));
            const auto beforeHashRejectArtifact=Read(artifact);
            auto forgedVersion=secondVersion;forgedVersion.Fingerprint=std::string(64,'f');
            Require(!editor.Load(request,forgedVersion,error)&&editor._CurrentSlot==heldSlot&&Read(artifact)==beforeHashRejectArtifact&&Read(heldPath)==heldBytes&&script->GetFields().size()==8,"Loaded image identity mismatch was accepted or modified active code/data");
            Write(QString::fromStdString(secondVersion.LibraryPath),candidateBytes+"\nchanged after compile\n");
            Require(!editor.Load(request,secondVersion,error)&&editor._CurrentSlot==heldSlot&&Read(artifact)==beforeHashRejectArtifact&&Read(heldPath)==heldBytes,"Loader accepted changed candidate bytes or damaged active state");
            Write(QString::fromStdString(secondVersion.LibraryPath),candidateBytes);
            Require(editor.Load(request,secondVersion,error),error);
            Require(editor._CurrentSlot==1-heldSlot&&editor._SlotLibraries[heldSlot]&&Read(heldPath)==heldBytes,"Successful replacement lost a still-mapped old slot");
            playVersion(2);
            auto reusable=compileVersion(1);
            const auto currentArtifact=Read(artifact),currentSlotBytes=Read(SlotFile(request,reusable,editor._CurrentSlot));
            for(int attempt=0;attempt<2;++attempt) {
                Require(!editor.Load(request,reusable,error)&&editor._CurrentSlot==1-heldSlot&&Read(heldPath)==heldBytes&&Read(artifact)==currentArtifact&&Read(SlotFile(request,reusable,editor._CurrentSlot))==currentSlotBytes,"Busy inactive slot was overwritten or a repeated retry changed the current library");
                Require(leasedEntry()->Version==GameSproutRuntime::ScriptModuleABIVersion&&leasedEntry()->Count==2,"Failed slot retirement invalidated an external user's function");
            }
            playVersion(2);
            Require(lease.unload(),"Cannot release old-slot lease: "+lease.errorString().toStdString());
            Require(editor.Load(request,reusable,error)&&editor._CurrentSlot==heldSlot,"Released slot could not be reused: "+error);
            CheckLoadedSlot(editor,request,reusable);playVersion(1);
        }
        for(const int version:{2,1,2}) {
            const int previousSlot=editor._CurrentSlot;
            auto candidate=compileVersion(version);
            Require(editor.Load(request,candidate,error),error);
            Require(editor._CurrentSlot==1-previousSlot,"Changed native version did not alternate the fixed slots");
            CheckLoadedSlot(editor,request,candidate);playVersion(version);
        }
        Require(QFile::remove(code+"/Aux/Independent.cpp")&&QDir().rmdir(code+"/Aux"),"Remove obsolete helper source");
        std::erase_if(request.Sources,[](const auto& source){return source.Path=="Code/Aux/Independent.cpp";});
        ScriptCompileResult removed;Require(compiler.Compile(request,removed,[]{return false;}),"Compile source removal: "+removed.Diagnostics);
        Require(!QFileInfo::exists(mirrorRoot+"/Code/Aux/Independent.cpp")&&!QFileInfo::exists(mirrorRoot+"/Code/Aux"),"Deleted source or empty directory remains in fixed Sources");
        Require(editor.Load(request,removed,error),error);CheckLoadedSlot(editor,request,removed);playVersion(codeVersion);
        // A failed B must not let the successful A record bypass restoring both
        // the source mirror and generated bindings when the user reverts to A.
        const QByteArray sourceA=Read(code+"/Mover.hpp"),generatedA=Read(generated);
        const QByteArray artifactA=Read(artifact),outputA=Read(QString::fromStdString(removed.LibraryPath));
        const QByteArray recordA=Read(QString::fromStdString(request.BuildRoot)+"/Build/ScriptBuildState.json");
        const int slotA=editor._CurrentSlot;
        QByteArray sourceB=sourceA;sourceB.replace("    float Added = 4.0f;","    float Added = 4.0f;\n    float FailedCandidateField = 9.0f;");
        sourceB.replace("static constexpr float MotionMultiplier = 2.0f;","static constexpr float MotionMultiplier = 3.0f;");
        Require(sourceB!=sourceA&&sourceB.contains("FailedCandidateField"),"Failure-revert fixture did not change field schema");
        Write(code+"/Mover.hpp",sourceB);Write(code+"/BrokenCandidate.cpp","void NativeTestBackendFailure(){asm(\"gamesprout_deliberately_invalid_instruction\");}\n");
        request.Sources.push_back({"Code/BrokenCandidate.cpp","61112233445566778899aabbccddeeff"});
        ScriptCompileResult failedB;Require(!compiler.Compile(request,failedB,[]{return false;})&&!failedB.Diagnostics.empty(),"Invalid auxiliary translation unit was accepted");
        Require(Read(generated)!=generatedA&&Read(mirrorRoot+"/Code/Mover.hpp")==sourceB,"Failed B did not exercise changed Sources and generated schema: "+failedB.Diagnostics);
        Require(editor._CurrentSlot==slotA&&Read(artifact)==artifactA&&Read(QString::fromStdString(removed.LibraryPath))==outputA&&Read(QString::fromStdString(request.BuildRoot)+"/Build/ScriptBuildState.json")==recordA,"Failed compile published a new record, library, Artifact or slot");
        Require(script->GetFields().size()==8&&!script->GetFields().contains("RenamedMover::FailedCandidateField"),"Failed build changed Runtime author data");playVersion(codeVersion);
        Write(code+"/Mover.hpp",sourceA);Require(QFile::remove(code+"/BrokenCandidate.cpp"),"Remove failed translation unit");request.Sources.pop_back();
        ScriptCompileResult restoredA;Require(compiler.Compile(request,restoredA,[]{return false;}),"Restore successful A: "+restoredA.Diagnostics);
        Require(restoredA.Fingerprint==removed.Fingerprint&&Read(mirrorRoot+"/Code/Mover.hpp")==sourceA&&Read(generated)==generatedA&&!QFileInfo::exists(mirrorRoot+"/Code/BrokenCandidate.cpp"),"Successful A cache record hid stale B source/generated files");
        Require(editor.Load(request,restoredA,error),error);CheckLoadedSlot(editor,request,restoredA);playVersion(codeVersion);
        ScriptCompileResult cancelled;Require(!compiler.Compile(request,cancelled,[]{return true;}),"Cancelled request did not abort");
        Write(code+"/Mover.hpp",sourceA+"\nsyntax error here\n");ScriptCompileResult failure;
        Require(!compiler.Compile(request,failure,[]{return false;})&&!failure.Diagnostics.empty(),"Invalid source has no readable diagnostic");
        Require(editor.GetScriptTypes().size()==2&&script->IsScriptTypeAvailable(),"Failed build replaced the previous loaded module");
        editor.Release(nullptr);Require(!script->IsScriptTypeAvailable(),"Unload did not leave a safe missing-type wrapper");
        delete editor._Runtime;
        temporary.setAutoRemove(true);
        std::cout<<"PASS plain five-callback single-marker source, typed self Add/Get, automatic GUID/field reflection, default values, distinct classes, explicit FixedUpdate, gameplay lifecycle logs, cache, atomic reload/rejection and schema identity, class rename identity, cancellation and safe unload\n";
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}

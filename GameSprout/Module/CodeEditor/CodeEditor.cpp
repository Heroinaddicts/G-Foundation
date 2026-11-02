#include "CodeEditor.h"
#include "CodeEditorBackend.h"
#include "IProject.h"
#include "IConsole.h"
#include "IIDEWindow.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <exception>
#include <limits>

namespace {
bool IsNativeSource(const std::string& path) {
    const QString extension=QFileInfo(QString::fromStdString(path)).suffix().toLower();
    return extension=="hpp"||extension=="h"||extension=="hxx"||extension=="inl"||extension=="cpp"||extension=="cc"||extension=="cxx";
}
struct ProjectJob {
    CodeEditorProjectRequest Request;
    CodeEditorProjectResult Result;
    QString Source;
    std::string Error;
    std::string OpenError;
    bool Generated=false;
};
}
CodeEditor::~CodeEditor(){Release(nullptr);}
void CodeEditor::BeforeInitialize(GameSproutEditor::GameSproutModuleApi* api) {
    if(!api||!api->_EditorApi) return;
    auto* editor=api->_EditorApi;
    _Project=editor->GetGameSproutModule<IProject>("Project");
    _Assets=editor->GetGameSproutModule<IAssetsManager>("AssetsManager");
    _Compiler=editor->GetGameSproutModule<IScriptCompiler>("ScriptCompiler");
    _Console=editor->GetGameSproutModule<IConsole>("Console");
    _IDE=editor->GetGameSproutView<IIDEWindow>("IDEWindow");
    _Tasks=api->_Foundation?api->_Foundation->TaskApi():nullptr;
}
bool CodeEditor::Initialize(GameSproutEditor::GameSproutModuleApi*) {
    if(!_CreateBackend) _CreateBackend=[]{return CodeEditorBackend::Create();};
    return _Project&&_Assets&&_Compiler&&_Tasks;
}
bool CodeEditor::Launch(GameSproutEditor::GameSproutModuleApi*) {
    if(_Launched) return true;
    _Owner=_Tasks->RegisterTaskOwner();
    if(_Owner==Foundation::InvalidTaskOwnerId) return false;
    _Dispatch=new QObject();
    _Subscribed=_Assets->RegisterAssetListener(this);
    if(!_Subscribed) {delete _Dispatch;_Dispatch=nullptr;_Tasks->UnregisterTaskOwner(_Owner);_Owner=Foundation::InvalidTaskOwnerId;return false;}
    _Launched=true;return true;
}
void CodeEditor::LaunchFinished(GameSproutEditor::GameSproutModuleApi*) {
    std::map<std::string,std::string> snapshot;
    for(const auto* meta:_Assets->GetAssetMetas())
        if(meta&&meta->_ImporterType==eAssetImporterType::Script&&IsNativeSource(meta->GetPath()))
            snapshot.emplace(meta->GetPath(),meta->_Guid.GetGuidString());
    if(snapshot!=_Sources) {_Sources.swap(snapshot);++_Revision;}
    _HasProject=QFileInfo::exists(QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Library/ScriptBuild/Xcode/GameLogic.xcodeproj"));
}
bool CodeEditor::ValidateSource(const std::string& relativePath,QString& absolute,std::string& error) const {
    error.clear();absolute.clear();
    const QString relative=QDir::cleanPath(QString::fromStdString(relativePath));
    if(relativePath.find('\0')!=std::string::npos||!IsNativeSource(relativePath)||QDir::isAbsolutePath(relative)||relative==".."||relative.startsWith("../")) {
        error="代码文件必须是项目 Assets 内的原生源文件。";return false;
    }
    const QString root=QFileInfo(QDir(QString::fromStdString(_Project->GetProjectRoot())).filePath("Assets")).canonicalFilePath();
    const QFileInfo source(QDir(root).filePath(relative));
    absolute=source.canonicalFilePath();
    if(root.isEmpty()||!source.isFile()||absolute.isEmpty()||!absolute.startsWith(root+'/')) {error="代码文件不存在或已经移出 Assets。";return false;}
    if(!_Sources.contains(relative.toStdString())) {error="代码文件尚未被资源管理器登记，请等待资源刷新完成。";return false;}
    return true;
}
bool CodeEditor::OpenSource(const std::string& path,std::string& error) {
    error.clear();
    if(!_Launched) {error="代码编辑器服务尚未就绪。";return false;}
    QString absolute;
    if(!ValidateSource(path,absolute,error)) return false;
    _PendingOpen=QDir::cleanPath(QString::fromStdString(path)).toStdString();
    ++_OpenSequence;Queue();return true;
}
void CodeEditor::OnAssetCreate(const AssetEvent& event){SourceEvent(event,false);}
void CodeEditor::OnAssetModify(const AssetEvent& event){SourceEvent(event,false);}
void CodeEditor::OnAssetRemove(const AssetEvent& event){SourceEvent(event,true);}
void CodeEditor::SourceEvent(const AssetEvent& event,bool removed) {
    bool changed=false;
    if(!event.OldPath.empty()&&event.OldPath!=event.Path) changed=_Sources.erase(event.OldPath)>0;
    if(removed||event.Importer!=eAssetImporterType::Script||!IsNativeSource(event.Path)) changed=(_Sources.erase(event.Path)>0)||changed;
    else {
        const auto guid=event.GuidValue.GetGuidString();
        const auto found=_Sources.find(event.Path);
        if(found==_Sources.end()||found->second!=guid) {_Sources.insert_or_assign(event.Path,guid);changed=true;}
    }
    if(!changed) return;
    ++_Revision;
    if(_HasProject) {_NeedsSync=true;Queue();}
}
void CodeEditor::Queue() {
    if(!_Launched||!_Dispatch||_Queued||_Running||(_PendingOpen.empty()&&!(_HasProject&&_NeedsSync))) return;
    _Queued=true;
    QTimer::singleShot(0,_Dispatch,[this]{_Queued=false;if(_Launched)Submit();});
}
void CodeEditor::Submit() {
    if(_Running||!_Launched) return;
    const auto toolchain=_Compiler->GetToolchain();
    ProjectJob initial;
    initial.Request.ProjectRoot=QString::fromStdString(_Project->GetProjectRoot());
    initial.Request.RuntimeSDKDirectory=QString::fromStdString(toolchain.RuntimeSDKDirectory);
    initial.Request.CMakeProgram=QString::fromStdString(toolchain.CMakeProgram);
    initial.Request.CompilerProgram=QString::fromStdString(toolchain.CompilerProgram);
    initial.Request.Configuration=QString::fromStdString(toolchain.Configuration);
    initial.Request.Architectures=QString::fromStdString(toolchain.Architectures);
    initial.Request.Sysroot=QString::fromStdString(toolchain.Sysroot);
    initial.Request.DeploymentTarget=QString::fromStdString(toolchain.DeploymentTarget);
    initial.Request.BaseFlags=QString::fromStdString(toolchain.BaseFlags);
    initial.Request.ConfigurationFlags=QString::fromStdString(toolchain.ConfigurationFlags);
#if defined(_WIN32)
    const QString toolName="Tools/GameSproutScriptBuild.exe";
#else
    const QString toolName="Tools/GameSproutScriptBuild";
#endif
    initial.Request.BuildToolProgram=QDir(QCoreApplication::applicationDirPath()).filePath(toolName);
    initial.Request.BuildToolArguments={"--ProjectRoot="+initial.Request.ProjectRoot};
    for(const auto& source:_Sources) initial.Request.Sources.push_back(QString::fromStdString(source.first));
    if(!_PendingOpen.empty()&&!ValidateSource(_PendingOpen,initial.Source,initial.OpenError)) {
        _PendingOpen.clear();
        // A file can disappear while the previous generation was running. Its
        // failed open must not suppress the queued removal from the IDE project.
        if(!(_HasProject&&_NeedsSync)) {Publish(false,_State.ProjectPath,initial.OpenError);return;}
    }
    const auto revision=_Revision,sequence=_OpenSequence;
    const auto createBackend=_CreateBackend;
    _NeedsSync=false;_Running=true;
    Publish(true,_State.ProjectPath,{});
    // A state listener may close the Editor synchronously. Do not submit work
    // after Release has detached the owner and its borrowed services.
    if(!_Launched||!_Tasks||_Owner==Foundation::InvalidTaskOwnerId){_Running=false;return;}
    if(_IDE){_IDE->Loading("准备代码工程",initial.Source.isEmpty()?"同步源文件列表":"GameLogic · Xcode");_Loading=true;}
    Foundation::TaskSubmitOptions options;options._OwnerId=_Owner;
    const auto submission=_Tasks->SubmitTask(options,
        [initial,createBackend](Int64& code,Foundation::ITaskContext& context) {
            auto* job=new ProjectJob(initial);
            if(!context.SetOwned(job)){delete job;code=-1;return false;}
            const auto cancelled=[&context]{return context.IsCancellationRequested();};
            CodeEditorBackend* backend=nullptr;
            try {
                // Owner cancellation suppresses callbacks but does not join the
                // worker. The worker therefore creates and destroys its adapter
                // locally, without borrowing an object released by this Module.
                backend=createBackend();
                if(!backend){job->Error="当前平台没有可用的代码编辑器后端。";code=-1;return false;}
                job->Generated=backend->Generate(job->Request,job->Result,job->Error,cancelled);
                bool success=job->Generated;
                if(success&&!job->Source.isEmpty())success=backend->Open(job->Result.ProjectPath,job->Source,1,job->Error,cancelled);
                if(success&&!job->OpenError.empty()){job->Error=job->OpenError;success=false;}
                delete backend;backend=nullptr;code=success?0:-1;return success;
            } catch(const std::exception& exception){job->Error=exception.what();}
              catch(...){job->Error="代码工程准备失败。";}
            delete backend;code=-1;return false;
        },
        [this,revision,sequence](const Foundation::TaskCompletion&,bool success,const Foundation::ITaskContext& context) {
            _Running=false;
            if(_Loading){_IDE->Unloading();_Loading=false;}
            const auto* job=context.Get<ProjectJob>();
            if(job&&job->Generated) _HasProject=true;
            const std::string projectPath=job&&job->Generated?job->Result.ProjectPath.toStdString():_State.ProjectPath;
            if(revision!=_Revision) {
                _NeedsSync=true;Publish(false,projectPath,{});Queue();return;
            }
            if(sequence==_OpenSequence) _PendingOpen.clear();
            const std::string error=success?std::string():job&&!job->Error.empty()?job->Error:"代码工程准备任务未完成。";
            Publish(false,projectPath,error);Queue();
        });
    if(!submission.Accepted()) {
        _Running=false;if(_Loading){_IDE->Unloading();_Loading=false;}
        if(sequence==_OpenSequence)_PendingOpen.clear();
        Publish(false,_State.ProjectPath,"TaskApi 拒绝了代码工程准备任务。");
    }
}
void CodeEditor::Publish(bool preparing,const std::string& projectPath,const std::string& error) {
    _State={preparing,projectPath,error};
    const auto snapshot=_State;
    const auto last=_NextListener;
    auto current=_Listeners.begin();
    while(current!=_Listeners.end()&&current->first<=last) {
        const auto id=current->first;
        try{current->second->OnCodeEditorStateChanged(snapshot);}catch(...){qWarning("Code editor listener threw an exception");}
        current=_Listeners.upper_bound(id);
    }
    if(!error.empty()) {
        if(_Console)_Console->ConsoleLog(__FILE__,__LINE__,eConsoleLog::Error,error);
        else qWarning().noquote()<<QString::fromStdString(error);
    }
}
bool CodeEditor::RegisterCodeEditorListener(ICodeEditorListener* listener) {
    if(!listener||_NextListener==std::numeric_limits<uint64_t>::max()) return false;
    for(const auto& entry:_Listeners)if(entry.second==listener)return false;
    _Listeners.emplace(++_NextListener,listener);return true;
}
void CodeEditor::UnregisterCodeEditorListener(ICodeEditorListener* listener) {
    for(auto item=_Listeners.begin();item!=_Listeners.end();)
        if(item->second==listener)item=_Listeners.erase(item);else ++item;
}
void CodeEditor::Release(GameSproutEditor::GameSproutModuleApi*) {
    _Launched=false;
    if(_Subscribed&&_Assets)_Assets->UnregisterAssetListener(this);
    _Subscribed=false;
    delete _Dispatch;_Dispatch=nullptr;_Queued=false;
    if(_Owner!=Foundation::InvalidTaskOwnerId&&_Tasks)_Tasks->UnregisterTaskOwner(_Owner);
    _Owner=Foundation::InvalidTaskOwnerId;_Running=false;
    if(_Loading&&_IDE)_IDE->Unloading();_Loading=false;
    _CreateBackend={};
    _Listeners.clear();_Sources.clear();_PendingOpen.clear();_NeedsSync=false;_HasProject=false;_State={};
    _Project=nullptr;_Assets=nullptr;_Compiler=nullptr;_Console=nullptr;_IDE=nullptr;_Tasks=nullptr;
}

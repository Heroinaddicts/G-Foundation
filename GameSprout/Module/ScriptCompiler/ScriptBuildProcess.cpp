#include "ScriptBuildProcess.h"
#include <QProcess>
#include <QThread>
#if defined(Q_OS_UNIX)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
class IsolatedProcess final : public QProcess {
#if defined(Q_OS_UNIX) && QT_VERSION < QT_VERSION_CHECK(6,0,0)
    void setupChildProcess() override {if(::setsid()<0) ::_exit(127);}
#endif
};
class ProcessTree final {
public:
    explicit ProcessTree(IsolatedProcess& process):_Process(process) {
#if defined(Q_OS_UNIX)
#if QT_VERSION >= QT_VERSION_CHECK(6,6,0)
        process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#elif QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        process.setChildProcessModifier([]{if(::setsid()<0) ::_exit(127);});
#endif
#elif defined(Q_OS_WIN)
#if defined(PROC_THREAD_ATTRIBUTE_JOB_LIST)
        _Job=::CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!_Job || !::SetInformationJobObject(_Job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {_Error="Cannot create native compiler process job";return;}
        SIZE_T size=0;
        ::InitializeProcThreadAttributeList(nullptr,2,0,&size);
        _Attributes=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(::HeapAlloc(::GetProcessHeap(),0,size));
        if(!_Attributes || !::InitializeProcThreadAttributeList(_Attributes,2,0,&size)) {_Error="Cannot initialize compiler process attributes";return;}
        _AttributesReady=true;
        process.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments* args){
            _Startup.StartupInfo=*reinterpret_cast<STARTUPINFOW*>(args->startupInfo);
            _Startup.StartupInfo.cb=sizeof(_Startup);
            _Startup.lpAttributeList=_Attributes;
            bool valid=::UpdateProcThreadAttribute(_Attributes,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&_Job,sizeof(_Job),nullptr,nullptr)!=FALSE;
            // Preserve the intended inherited stdio handles while adding a job
            // attribute at CreateProcess time; no child can escape before attach.
            const HANDLE handles[]={_Startup.StartupInfo.hStdInput,_Startup.StartupInfo.hStdOutput,_Startup.StartupInfo.hStdError};
            SIZE_T count=0;
            for(HANDLE handle:handles) if(handle && handle!=INVALID_HANDLE_VALUE) {
                bool duplicate=false;for(SIZE_T i=0;i<count;++i) if(_Handles[i]==handle) duplicate=true;
                if(!duplicate) _Handles[count++]=handle;
            }
            if(args->inheritHandles && count) valid=valid && ::UpdateProcThreadAttribute(_Attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,_Handles,count*sizeof(HANDLE),nullptr,nullptr)!=FALSE;
            if(!valid) {_Error="Cannot attach native compiler process tree to its job";args->applicationName=L"";return;}
            args->startupInfo=reinterpret_cast<decltype(args->startupInfo)>(&_Startup.StartupInfo);
            args->flags|=EXTENDED_STARTUPINFO_PRESENT;
        });
#else
        _Error="The Windows SDK lacks atomic compiler process job attachment";
#endif
#endif
    }
    ~ProcessTree() {
        StopAndWait();
#if defined(Q_OS_WIN)
        if(_AttributesReady) ::DeleteProcThreadAttributeList(_Attributes);
        if(_Attributes) ::HeapFree(::GetProcessHeap(),0,_Attributes);
        if(_Job) ::CloseHandle(_Job);
#endif
    }
    const QString& Error() const {return _Error;}
    void Started() {if(_Process.processId()>0) _PID=_Process.processId();}
    void StopAndWait() {
        if(_Finished || _PID<=0) return;
#if defined(Q_OS_UNIX)
        ::kill(-static_cast<pid_t>(_PID),SIGKILL);
#elif defined(Q_OS_WIN)
        if(_Job) ::TerminateJobObject(_Job,ERROR_CANCELLED);
#endif
        while(_Process.state()!=QProcess::NotRunning) _Process.waitForFinished(50);
        // Parent termination alone is insufficient: Ninja/Clang may still own
        // files in fixed Build. Keep the compile lock until the whole tree exits.
        while(Alive()) {
#if defined(Q_OS_UNIX)
            ::kill(-static_cast<pid_t>(_PID),SIGKILL);
#elif defined(Q_OS_WIN)
            if(_Job) ::TerminateJobObject(_Job,ERROR_CANCELLED);
#endif
            QThread::msleep(10);
        }
        _Finished=true;
    }
    void Finish() {
        // Normally the tool has joined its children. Also settle unexpected
        // descendants after a crash instead of letting them race the next build.
        if(Alive()) StopAndWait();else _Finished=true;
    }
private:
    bool Alive() const {
#if defined(Q_OS_UNIX)
        return _PID>0 && (::kill(-static_cast<pid_t>(_PID),0)==0 || errno==EPERM);
#elif defined(Q_OS_WIN)
        if(!_Job) return false;
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if(!::QueryInformationJobObject(_Job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)) return true;
        return accounting.ActiveProcesses!=0;
#else
        return _Process.state()!=QProcess::NotRunning;
#endif
    }
    IsolatedProcess& _Process;
    QString _Error;
    qint64 _PID=0;
    bool _Finished=false;
#if defined(Q_OS_WIN)
    HANDLE _Job=nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST _Attributes=nullptr;
    bool _AttributesReady=false;
    STARTUPINFOEXW _Startup{};
    HANDLE _Handles[3]{};
#endif
};
}

bool ScriptBuildProcess::Run(const QString& program,const QStringList& arguments,const QString& directory,
    bool mergeChannels,qsizetype outputLimit,Result& result,const std::function<bool()>& cancelled) {
    result={};
    if(cancelled && cancelled()) {result.Cancelled=true;result.Error="Native script process cancelled";return false;}
    IsolatedProcess process;
    ProcessTree tree(process);
    if(!tree.Error().isEmpty()) {result.Error=tree.Error();return false;}
    process.setWorkingDirectory(directory);
    process.setProcessChannelMode(mergeChannels?QProcess::MergedChannels:QProcess::SeparateChannels);
    process.start(program,arguments);tree.Started();
    if(!process.waitForStarted(10000)) {tree.Started();tree.StopAndWait();result.Error=tree.Error().isEmpty()?process.errorString():tree.Error();return false;}
    tree.Started();
    while(!process.waitForFinished(50)) {
        result.Output+=process.readAllStandardOutput();
        if(!mergeChannels) result.ErrorOutput+=process.readAllStandardError();
        if((cancelled && cancelled()) || result.Output.size()>outputLimit) {
            result.Cancelled=result.Output.size()<=outputLimit;
            result.Error=result.Cancelled?"Native script process cancelled":"Native script process output exceeded the supported limit";
            tree.StopAndWait();return false;
        }
        if(result.ErrorOutput.size()>1024*1024) result.ErrorOutput=result.ErrorOutput.right(1024*1024);
    }
    tree.Finish();
    result.Output+=process.readAllStandardOutput();if(!mergeChannels) result.ErrorOutput+=process.readAllStandardError();
    if(result.Output.size()>outputLimit) {result.Error="Native script process output exceeded the supported limit";return false;}
    return process.exitStatus()==QProcess::NormalExit && process.exitCode()==0;
}

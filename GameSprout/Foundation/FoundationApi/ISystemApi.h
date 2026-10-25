#ifndef SYSTEM_API_H
#define SYSTEM_API_H

#include "MultiSys.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Foundation {
    inline constexpr std::size_t SystemIpcMaximumMessageSize = 16 * 1024 * 1024;

    enum class eSystemErrorCode : int {
        None,
        InvalidArgument,
        SystemUnavailable,
        ProcessLaunchFailed,
        ProcessIoFailed,
        IpcListenFailed,
        IpcConnectFailed,
        IpcAuthenticationFailed,
        IpcMessageTooLarge,
        IpcClosed,
        PlatformFailure
    };

    struct SystemError final {
        eSystemErrorCode _Code = eSystemErrorCode::None;
        Int64 _PlatformCode = 0;
        std::string _Message;

        bool Succeeded() const {
            return _Code == eSystemErrorCode::None;
        }
    };

    enum class eSystemProcessExitReason : int {
        Exited,
        Terminated,
        LaunchFailed
    };

    struct SystemProcessEnvironmentVariable final {
        std::string _Name;
        std::string _Value;
    };

    struct SystemProcessStartInfo final {
        std::string _Executable;
        std::vector<std::string> _Arguments;
        // Variables override only the child environment. Names and values must
        // be valid UTF-8 and are never included in process diagnostics.
        std::vector<SystemProcessEnvironmentVariable> _Environment;
        std::string _WorkingDirectory;
        bool _Hidden = true;
        bool _CaptureOutput = false;
        // Keeps the child inside the Foundation owner's lifetime boundary.
        // Windows uses a kill-on-close Job Object; Linux uses a parent-death
        // signal. Normal callers must still request an orderly shutdown first.
        bool _TerminateWithOwner = false;
    };

    struct SystemProcessExitInfo final {
        Int64 _ExitCode = -1;
        eSystemProcessExitReason _Reason = eSystemProcessExitReason::LaunchFailed;
    };

    struct SystemProcessCallbacks final {
        // Output and error notifications are always dispatched before _Exited.
        // This ordering lets callers release callback-owned capture state from
        // the exit notification without racing a late pipe notification.
        std::function<void(const std::string&)> _OutputReceived;
        std::function<void(const std::string&)> _ErrorReceived;
        std::function<void(const SystemProcessExitInfo&)> _Exited;
    };

    class ISystemProcess {
    public:
        virtual ~ISystemProcess() {}

        virtual UInt64 GetProcessId() const = 0;
        virtual bool IsRunning() const = 0;
        virtual bool Terminate(SystemError* error = nullptr) = 0;
        // Releases the process object. A still-running child is terminated first.
        virtual void Release() = 0;
    };

    struct SystemIpcEndpoint final {
        std::string _Address;
        std::string _AccessToken;

        bool IsValid() const {
            return !_Address.empty() && !_AccessToken.empty();
        }
    };

    class ISystemIpcConnection;

    struct SystemIpcConnectionCallbacks final {
        std::function<void()> _Connected;
        std::function<void(const std::vector<UInt8>&)> _MessageReceived;
        std::function<void()> _Disconnected;
        std::function<void(const SystemError&)> _ErrorOccurred;
    };

    struct SystemIpcServerCallbacks final {
        std::function<void(ISystemIpcConnection*)> _ConnectionAccepted;
        std::function<void(ISystemIpcConnection*, const std::vector<UInt8>&)> _MessageReceived;
        std::function<void(ISystemIpcConnection*)> _ConnectionClosed;
        std::function<void(const SystemError&)> _ErrorOccurred;
    };

    class ISystemIpcConnection {
    public:
        virtual ~ISystemIpcConnection() {}

        virtual bool IsConnected() const = 0;
        // Copies the message into the internal send queue and returns immediately.
        virtual bool Send(const void* data, std::size_t size, SystemError* error = nullptr) = 0;
        virtual void Close() = 0;
        // Release invalidates this pointer. Destruction is deferred behind callbacks
        // already queued for the next Foundation Update().
        virtual void Release() = 0;
    };

    class ISystemIpcServer {
    public:
        virtual ~ISystemIpcServer() {}

        virtual const SystemIpcEndpoint& GetEndpoint() const = 0;
        virtual void Close() = 0;
        // Closing or releasing the server also closes its accepted connections.
        virtual void Release() = 0;
    };

    enum class eWatcherType : uint8_t {
        File = 0,
        Directory = 1
    };

    enum class eFileSystemAction : uint8_t {
        Create = 0,
        Modify= 1,
        Delete = 2,
        Rename = 3 // Files only. Directory renames emit DeteleDir then CreateDir.
    };

    struct FileSystemWatcherEvent {
        eWatcherType Type = eWatcherType::File;
        eFileSystemAction Action = eFileSystemAction::Modify;
        std::string Path;    // Current absolute UTF-8 path; deleted path for deletion.
        std::string OldPath; // Files only: set for Rename / extension-case-only ModifyFile.
    };

    // event/extension are borrowed for this call; copy them to retain them.
    // extension is Path's final suffix including '.', preserving case (e.g. ".PNG").
    // Directories and files with no extension receive ""; deleted files use Path's suffix.
    typedef std::function<void(const FileSystemWatcherEvent& event,
                               const std::string& extension, void* context)> fOnWatcherCallback;

    class ISystemApi {
    public:
        virtual ~ISystemApi() {}

        // Process and IPC callbacks are dispatched by FoundationApi::Update()
        // on the thread that calls Update().
        virtual ISystemProcess* StartProcess(const SystemProcessStartInfo& startInfo, const SystemProcessCallbacks& callbacks, SystemError* error = nullptr) = 0;
        virtual ISystemIpcServer* CreateLocalIpcServer(const SystemIpcServerCallbacks& callbacks, SystemError* error = nullptr) = 0;
        virtual ISystemIpcConnection* ConnectLocalIpc(const SystemIpcEndpoint& endpoint, const SystemIpcConnectionCallbacks& callbacks, SystemError* error = nullptr) = 0;

        // Register, Update and Release on the same main thread. Direct children only
        // for directory watches; the watched directory itself is never reported.
        // Removing, renaming or replacing that directory silently ends its watch.
        // Watch its parent to receive events about the directory itself.
        // Same-directory file renames are paired when observed
        // within a short batch: Rename, or ModifyFile for extension case changes.
        // Directory renames emit DeteleDir(old) then CreateDir(new), both with no OldPath.
        // File watches follow such renames; deleting the file ends its subscription
        // after DeleteFile. Keep context/captures alive through Foundation Release
        // unless a file watch has delivered its terminal event.
        virtual bool FileSystemWatcher(const std::string & path, void * const context, const fOnWatcherCallback callback) = 0;
    };
}

#endif // SYSTEM_API_H

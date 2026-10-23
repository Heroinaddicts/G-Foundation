#ifndef SYSTEM_H
#define SYSTEM_H

#include "Interface/ISystem.h"

#include <deque>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace Foundation {
    class SystemProcess;
    class SystemIpcConnection;
    class SystemIpcServer;
    class SystemFileWatchers;

    class System final : public ISystem {
    public:
        ~System() override;

        bool Initialize(FoundationApi* foundation) override;
        bool Launch(FoundationApi* foundation) override;
        void Release(FoundationApi* foundation) override;
        void EarlyUpdate(FoundationApi* foundation) override;
        void Update(FoundationApi* foundation) override;
        void LaterUpdate(FoundationApi* foundation) override;

        ISystemProcess* StartProcess(
            const SystemProcessStartInfo& startInfo,
            const SystemProcessCallbacks& callbacks,
            SystemError* error
        ) override;
        ISystemIpcServer* CreateLocalIpcServer(
            const SystemIpcServerCallbacks& callbacks,
            SystemError* error
        ) override;
        ISystemIpcConnection* ConnectLocalIpc(
            const SystemIpcEndpoint& endpoint,
            const SystemIpcConnectionCallbacks& callbacks,
            SystemError* error
        ) override;


        // Register and call Update()/Release() on the same owner thread.
        // Existing file: watch that file. Existing directory: direct children only.
        // Callbacks receive UTF-8 absolute paths and OldPath for recognized renames.
        // File watches follow same-directory renames. Renamed child directories
        // emit a deletion/creation pair. The watched directory itself is never
        // reported; its removal, rename or replacement silently ends the watch.
        // On removal, cached direct-child deletions are delivered before ending
        // the watch; registered descendants finish before parent directory deletion.
        // extension is empty for directories. Native events can coalesce.
        // context is borrowed; keep it and callback captures alive until Release()
        // or the target's terminal callback. Release() cancels queued events.
        bool FileSystemWatcher(const std::string & path, void * const context, fOnWatcherCallback callback) override;

        void PostCallback(std::function<void()> callback);
        void RegisterConnection(SystemIpcConnection* connection);
        void ReleaseProcess(SystemProcess* process);
        void ReleaseServer(SystemIpcServer* server);
        void ReleaseConnection(SystemIpcConnection* connection);

    private:
        friend class Foundation;
        System() {}
        System(const System&) = delete;
        System& operator=(const System&) = delete;

        // Only the owner thread registers/dispatches/stops. The implementation
        // owns one joined libuv worker and queues copied events across threads.
        SystemFileWatchers* _FileSystemWatchers = nullptr;
        std::mutex _CallbackMutex;
        std::deque<std::function<void()>> _Callbacks;
        std::mutex _ObjectMutex;
        std::unordered_set<SystemProcess*> _Processes;
        std::unordered_set<SystemIpcServer*> _Servers;
        std::unordered_set<SystemIpcConnection*> _Connections;
        FoundationApi* _Foundation = nullptr;
        bool _Launched = false;
    };
}

#endif // SYSTEM_H

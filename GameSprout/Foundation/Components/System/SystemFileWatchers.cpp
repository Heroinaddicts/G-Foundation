#include "SystemFileWatchers.h"
#include "SafeQueue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <sys/stat.h>
#include <thread>
#include <utility>
#include <vector>

#include <uv.h>

namespace Foundation {
    namespace {
        std::filesystem::path NativePath(const std::string& value) {
            return std::filesystem::path(std::u8string(value.begin(), value.end()));
        }

        std::string Utf8Path(const std::filesystem::path& value) {
            const std::u8string text = value.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        bool ExtensionCaseOnly(const std::string& oldPath, const std::string& newPath) {
            const std::filesystem::path oldName = NativePath(oldPath), newName = NativePath(newPath);
            if (oldName.parent_path() != newName.parent_path() || oldName.stem() != newName.stem()) {
                return false;
            }
            const std::string oldExtension = Utf8Path(oldName.extension());
            const std::string newExtension = Utf8Path(newName.extension());
            if (oldExtension.empty() || oldExtension == newExtension ||
                oldExtension.size() != newExtension.size()) {
                return false;
            }
            const auto fold = [](unsigned char value) {
                return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
            };
            for (std::size_t i = 0; i < oldExtension.size(); ++i) {
                if (fold(oldExtension[i]) != fold(newExtension[i])) {
                    return false;
                }
            }
            return true;
        }

        using FileIdentity = std::pair<uint64_t, uint64_t>;

        struct Entry {
            bool Directory = false;
            FileIdentity Identity{}; // Device and inode/file ID; hard links share this.
            std::uintmax_t Size = 0;
            uv_timespec_t Modified{};
            uv_timespec_t Changed{};
            uv_timespec_t Birth{};
        };

        enum class EntryResult { Present, Missing, Failed, Other };

        EntryResult FileError(const int error) {
            return error == UV_ENOENT || error == UV_ENOTDIR
                ? EntryResult::Missing : EntryResult::Failed;
        }

        EntryResult ReadEntry(const std::string& path, Entry& entry) {
            uv_fs_t request{};
            const int result = uv_fs_lstat(nullptr, &request, path.c_str(), nullptr);
            const uv_stat_t status = request.statbuf;
            uv_fs_req_cleanup(&request);
            if (result < 0) {
                return FileError(result);
            }
#if defined(_WIN32)
            const auto type = status.st_mode & _S_IFMT;
            entry.Directory = type == _S_IFDIR;
            const bool regular = type == _S_IFREG;
#else
            entry.Directory = S_ISDIR(status.st_mode);
            const bool regular = S_ISREG(status.st_mode);
#endif
            if (!entry.Directory && !regular) {
                return EntryResult::Other; // Do not follow symlinks out of the watched tree.
            }
            entry.Identity = {status.st_dev, status.st_ino};
            entry.Size = status.st_size;
            entry.Modified = status.st_mtim;
            entry.Changed = status.st_ctim;
            entry.Birth = status.st_birthtim;
            return EntryResult::Present;
        }

        EntryResult ResolvePath(const std::string& path, std::string& actual) {
            uv_fs_t request{};
            const int result = uv_fs_realpath(nullptr, &request, path.c_str(), nullptr);
            if (result < 0) {
                uv_fs_req_cleanup(&request);
                return FileError(result);
            }
            try {
                // realpath returns the filesystem's spelling on macOS/Windows.
                actual = Utf8Path(NativePath(static_cast<const char*>(request.ptr)).lexically_normal());
            } catch (...) {
                uv_fs_req_cleanup(&request);
                throw;
            }
            uv_fs_req_cleanup(&request);
            return EntryResult::Present;
        }

        bool SameTime(const uv_timespec_t& left, const uv_timespec_t& right) {
            return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
        }

        bool FileChanged(const Entry& previous, const Entry& current) {
            return previous.Identity != current.Identity || previous.Size != current.Size ||
                !SameTime(previous.Modified, current.Modified) ||
                !SameTime(previous.Changed, current.Changed) ||
                !SameTime(previous.Birth, current.Birth);
        }

        bool ScanChildren(const std::string& path, std::map<std::string, Entry>& entries) {
            std::error_code error;
            std::filesystem::directory_iterator iterator(NativePath(path), error);
            const std::filesystem::directory_iterator end;
            if (error) {
                return false;
            }
            std::map<std::string, Entry> result;
            while (iterator != end) {
                const std::string child = Utf8Path(iterator->path());
                Entry entry;
                const EntryResult state = ReadEntry(child, entry);
                if (state == EntryResult::Failed) {
                    return false; // An unreadable directory must not look empty.
                }
                if (state == EntryResult::Present) {
                    result.emplace(child, entry);
                }
                iterator.increment(error);
                if (error) {
                    return false;
                }
            }
            entries.swap(result);
            return true;
        }

    }

    struct SystemFileWatchers::State {

        struct Watch;

        struct Handle {
            uv_fs_event_t Native{};
            Watch* Owner = nullptr;
            bool Parent = false;
            bool Initialized = false;
        };

        struct Watch {
            State* Service = nullptr;
            UInt64 ID = 0;
            std::string Path;
            std::string ParentPath;
            Entry Self;
            std::map<std::string, Entry> Children;
            std::map<FileIdentity, std::set<std::string>> PathsByIdentity;
            std::set<std::string> DirtyChildren;
            std::set<std::string> ParentCandidates;
            bool RescanChildren = false;
            bool RescanParent = false;
            Handle ParentHandle;
            Handle ContentsHandle;
            unsigned OpenHandles = 0;
            bool Closing = false;
        };

        struct Registration {
            void* Context = nullptr; // Borrowed until Stop(), not owned by Foundation.
            fOnWatcherCallback Callback;
        };

        struct Event {
            UInt64 ID;
            FileSystemWatcherEvent Data;
            std::string Extension;
            bool Terminal = false;
            bool Notify = true;
        };

        struct Command {
            UInt64 ID = 0;
            std::string Path;
            std::atomic<bool> Done{false};
            bool Success = false;
            std::string Error;
        };

        const std::thread::id OwnerThread = std::this_thread::get_id();
        // Registrations and dispatch state are touched exclusively by OwnerThread.
        std::map<UInt64, Registration> Registrations;
        UInt64 NextID = 0;
        bool Started = false;
        bool Stopped = false;
        bool Dispatching = false;

        uv_loop_t Loop{};
        uv_async_t Wakeup{};
        uv_timer_t BatchTimer{};
        std::set<UInt64> DirtyWatches; // Worker-only; never scan every registered watch.
        std::thread Worker;
        // Native handle ownership and Watches belong exclusively to Worker.
        std::map<UInt64, Watch*> Watches;
        std::map<std::string, std::set<UInt64>> DirectoryWatches;
        std::atomic<bool> StopRequested{false};
        // The main thread submits one synchronous registration at a time.
        // This member outlives Worker, including its final Done.notify_one().
        Command RegistrationCommand;
        std::atomic<Command*> PendingCommand{nullptr};
        // Worker produces; the main thread consumes in Dispatch(), or drains
        // after joining Worker in Stop(). Queue ownership transfers Event*.
        FoundationUtils::SafeQueue::SpscQueue<Event*> Events;

        ~State() {
            Stop();
        }

        static void HandleClosed(uv_handle_t* native) {
            Handle* handle = static_cast<Handle*>(native->data);
            Watch* watch = handle->Owner;
            if (--watch->OpenHandles == 0) {
                delete watch;
            }
        }

        void CloseWatch(Watch* watch) {
            if (watch->Closing) {
                return;
            }
            watch->Closing = true;
            Watches.erase(watch->ID);
            if (watch->Self.Directory) {
                const auto indexed = DirectoryWatches.find(watch->Path);
                if (indexed != DirectoryWatches.end()) {
                    indexed->second.erase(watch->ID);
                    if (indexed->second.empty()) {
                        DirectoryWatches.erase(indexed);
                    }
                }
            }
            DirtyWatches.erase(watch->ID);
            for (Handle* handle : {&watch->ParentHandle, &watch->ContentsHandle}) {
                if (handle->Initialized) {
                    uv_fs_event_stop(&handle->Native);
                    uv_close(reinterpret_cast<uv_handle_t*>(&handle->Native), &HandleClosed);
                }
            }
            if (watch->OpenHandles == 0) {
                delete watch;
            }
        }

        void Queue(Event* event) {
            try {
                Events.Push(event);
            } catch (...) {
                delete event;
                throw;
            }
        }

        void Queue(Watch* watch, const std::string& path,
                   const eFileSystemAction action, const bool directory,
                   const bool terminal = false, const std::string& oldPath = {}) {
            Queue(new Event{watch->ID,
                {directory ? eWatcherType::Directory : eWatcherType::File,
                 action, path, oldPath},
                directory ? std::string{} : Utf8Path(NativePath(path).extension()), terminal});
        }

        void EndDirectoryWatch(Watch* watch, const bool removed) {
            if (removed) {
                // The directory is already gone: use the last known direct children.
                // RemoveChild also finishes registered descendants before their parent.
                while (!watch->Children.empty()) {
                    RemoveChild(watch, watch->Children.begin()->first);
                }
            }
            // Retire the main-thread registration without reporting the root itself.
            Queue(new Event{watch->ID, {}, {}, true, false});
            CloseWatch(watch);
        }

        void RetireDirectoryTree(const std::string& path, const bool removed) {
            // Copy IDs before closing watches; closing changes both worker indexes.
            std::vector<std::pair<std::size_t, UInt64>> pending;
            const auto root = DirectoryWatches.find(path);
            if (root != DirectoryWatches.end()) {
                for (const UInt64 id : root->second) {
                    pending.emplace_back(path.size(), id);
                }
            }
            const std::string prefix = path.back() == '/' ? path : path + '/';
            for (auto entry = DirectoryWatches.lower_bound(prefix);
                 entry != DirectoryWatches.end() && entry->first.starts_with(prefix); ++entry) {
                for (const UInt64 id : entry->second) {
                    pending.emplace_back(entry->first.size(), id);
                }
            }
            std::sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
                return left.first != right.first ? left.first > right.first : left.second < right.second;
            });
            for (const auto& [depth, id] : pending) {
                const auto found = Watches.find(id);
                if (found != Watches.end()) {
                    EndDirectoryWatch(found->second, removed);
                }
            }
        }

        bool RenameSelf(Watch* watch, const std::string& actual, const Entry& current) {
            const bool terminal =
                NativePath(actual).parent_path() != NativePath(watch->Path).parent_path();
            const auto action = ExtensionCaseOnly(watch->Path, actual)
                ? eFileSystemAction::Modify : eFileSystemAction::Rename;
            Queue(watch, actual, action, current.Directory, terminal, watch->Path);
            if (terminal) {
                CloseWatch(watch);
                return false;
            }
            watch->Path = actual; // Parent handle stays valid for same-directory file renames.
            watch->Self = current;
            return true;
        }

        bool CheckSelf(Watch* watch, const std::set<std::string>& candidates, bool rescanParent) {
            Entry current;
            EntryResult state = ReadEntry(watch->Path, current);
            if (state == EntryResult::Failed) {
                return false;
            }
            std::string actual;
            if (state == EntryResult::Present) {
                state = ResolvePath(watch->Path, actual);
                if (state == EntryResult::Failed) {
                    return false;
                }
            }
            if (state == EntryResult::Missing) {
                std::set<std::string> possible = candidates;
                if (rescanParent || watch->Self.Directory) {
                    std::map<std::string, Entry> siblings;
                    if (ScanChildren(watch->ParentPath, siblings)) {
                        for (const auto& [path, entry] : siblings) {
                            if (entry.Identity == watch->Self.Identity) {
                                possible.insert(path);
                            }
                        }
                    }
                }
                for (const auto& path : possible) {
                    if (ReadEntry(path, current) == EntryResult::Present &&
                        current.Identity == watch->Self.Identity &&
                        current.Directory == watch->Self.Directory &&
                        ResolvePath(path, actual) == EntryResult::Present) {
                        if (watch->Self.Directory) {
                            // Its contents still exist under the new name; do not
                            // turn a directory rename into fictitious file deletions.
                            RetireDirectoryTree(watch->Path, false);
                            return false;
                        }
                        return RenameSelf(watch, actual, current);
                    }
                }
            }
            if (watch->Self.Directory) {
                if (state != EntryResult::Present || !current.Directory ||
                    current.Identity != watch->Self.Identity || actual != watch->Path) {
                    const bool removed = state != EntryResult::Present || !current.Directory ||
                        current.Identity != watch->Self.Identity;
                    RetireDirectoryTree(watch->Path, removed);
                    return false;
                }
                watch->Self = current;
                return true;
            }
            if (state != EntryResult::Present || current.Directory != watch->Self.Directory ||
                (actual != watch->Path && current.Identity != watch->Self.Identity)) {
                Queue(watch, watch->Path, eFileSystemAction::Delete, watch->Self.Directory, true);
                CloseWatch(watch);
                return false;
            }
            if (actual != watch->Path) {
                return RenameSelf(watch, actual, current);
            }
            if (!current.Directory && FileChanged(watch->Self, current)) {
                Queue(watch, watch->Path, eFileSystemAction::Modify, false);
            }
            watch->Self = current;
            return true;
        }

        void RemoveIndex(Watch* watch, const FileIdentity& identity, const std::string& path) {
            const auto found = watch->PathsByIdentity.find(identity);
            if (found != watch->PathsByIdentity.end()) {
                found->second.erase(path);
                if (found->second.empty()) {
                    watch->PathsByIdentity.erase(found);
                }
            }
        }

        void RemoveChild(Watch* watch, const std::string& path) {
            const auto found = watch->Children.find(path);
            if (found != watch->Children.end()) {
                if (found->second.Directory) {
                    RetireDirectoryTree(path, true);
                }
                Queue(watch, path, eFileSystemAction::Delete, found->second.Directory);
                RemoveIndex(watch, found->second.Identity, path);
                watch->Children.erase(found);
            }
        }

        bool RelocateChild(Watch* watch, const std::string& path, const Entry& current) {
            const auto names = watch->PathsByIdentity.find(current.Identity);
            if (names == watch->PathsByIdentity.end() || current.Identity.second == 0) {
                return false;
            }
            const auto existing = watch->Children.find(path);
            if (existing != watch->Children.end() && existing->second.Identity == current.Identity) {
                return false; // An existing hard-link name is not a newly renamed entry.
            }
            std::string oldPath;
            for (const std::string& candidate : names->second) {
                if (candidate == path) {
                    continue;
                }
                std::string actual;
                const EntryResult state = ResolvePath(candidate, actual);
                if (state == EntryResult::Missing ||
                    (state == EntryResult::Present && actual != candidate)) {
                    if (!oldPath.empty()) {
                        return false; // Ambiguous deleted hard links: do not guess asset identity.
                    }
                    oldPath = candidate;
                }
            }
            if (oldPath.empty()) {
                return false;
            }
            if (existing != watch->Children.end()) {
                RemoveChild(watch, path); // A rename may overwrite an unrelated destination.
            }
            const auto previous = watch->Children.find(oldPath);
            const Entry oldEntry = previous->second;
            RemoveIndex(watch, oldEntry.Identity, oldPath);
            watch->Children.erase(previous);
            watch->Children.emplace(path, current);
            watch->PathsByIdentity[current.Identity].insert(path);
            if (current.Directory) {
                RetireDirectoryTree(oldPath, false);
                Queue(watch, oldPath, eFileSystemAction::Delete, true);
                Queue(watch, path, eFileSystemAction::Create, true);
            } else {
                const auto action = ExtensionCaseOnly(oldPath, path)
                    ? eFileSystemAction::Modify : eFileSystemAction::Rename;
                Queue(watch, path, action, false, false, oldPath);
            }
            return true;
        }

        void ObservePresentChild(Watch* watch, const std::string& actual, const Entry& current) {
            if (RelocateChild(watch, actual, current)) {
                return;
            }
            auto previous = watch->Children.find(actual);
            if (previous != watch->Children.end() && previous->second.Directory != current.Directory) {
                RemoveChild(watch, actual);
                previous = watch->Children.end();
            }
            if (previous == watch->Children.end()) {
                Queue(watch, actual, eFileSystemAction::Create, current.Directory);
                watch->Children.emplace(actual, current);
            } else {
                if (!current.Directory && FileChanged(previous->second, current)) {
                    Queue(watch, actual, eFileSystemAction::Modify, false);
                }
                if (previous->second.Identity != current.Identity) {
                    RemoveIndex(watch, previous->second.Identity, actual);
                }
                previous->second = current;
            }
            watch->PathsByIdentity[current.Identity].insert(actual);
        }

        EntryResult ObserveChild(Watch* watch, const std::string& path) {
            Entry current;
            EntryResult state = ReadEntry(path, current);
            if (state != EntryResult::Present) {
                return state;
            }
            std::string actual;
            state = ResolvePath(path, actual);
            if (state != EntryResult::Present) {
                return state;
            }
            if (NativePath(actual).parent_path() != NativePath(watch->Path)) {
                return EntryResult::Failed;
            }
            ObservePresentChild(watch, actual, current);
            return EntryResult::Present;
        }

        void Rescan(Watch* watch) {
            std::map<std::string, Entry> current;
            if (!ScanChildren(watch->Path, current)) {
                return;
            }
            // Pair identities before removing disappeared paths, also in overflow recovery.
            for (const auto& [path, entry] : current) {
                RelocateChild(watch, path, entry);
            }
            for (auto previous = watch->Children.begin(); previous != watch->Children.end();) {
                const auto old = previous++;
                const auto found = current.find(old->first);
                if (found == current.end() || found->second.Directory != old->second.Directory) {
                    RemoveChild(watch, old->first);
                }
            }
            for (const auto& [path, entry] : current) {
                ObservePresentChild(watch, path, entry);
            }
        }

        static void FlushChanges(uv_timer_t* timer) {
            State* service = static_cast<State*>(timer->data);
            std::set<UInt64> pending;
            pending.swap(service->DirtyWatches);
            std::vector<std::pair<std::size_t, UInt64>> ordered;
            try {
                for (const UInt64 id : pending) {
                    const auto found = service->Watches.find(id);
                    if (found != service->Watches.end()) {
                        ordered.emplace_back(found->second->Path.size(), id);
                    }
                }
                // Ancestors reconcile first, so directory renames can retire old
                // subtree watches before those watches mistake the move for removal.
                std::sort(ordered.begin(), ordered.end());
            } catch (...) {
                service->DirtyWatches.merge(pending);
                uv_timer_start(&service->BatchTimer, &FlushChanges, 30, 0);
                std::fputs("[Foundation][System] Failed to order filesystem changes.\n", stderr);
                return;
            }
            for (const auto& [depth, id] : ordered) {
                const auto found = service->Watches.find(id);
                if (found == service->Watches.end()) {
                    continue;
                }
                Watch* watch = found->second;
                try {
                    std::set<std::string> children, parents;
                    children.swap(watch->DirtyChildren);
                    parents.swap(watch->ParentCandidates);
                    const bool rescan = watch->RescanChildren;
                    const bool rescanParent = watch->RescanParent;
                    watch->RescanChildren = false;
                    watch->RescanParent = false;
                    if (!service->CheckSelf(watch, parents, rescanParent) || !watch->Self.Directory) {
                        continue;
                    }
                    if (rescan) {
                        service->Rescan(watch);
                    } else {
                        std::set<std::string> missing;
                        for (const auto& path : children) {
                            const EntryResult state = service->ObserveChild(watch, path);
                            if (state == EntryResult::Missing || state == EntryResult::Other) {
                                missing.insert(path);
                            }
                        }
                        // Present/new paths get the first chance to claim an old file identity.
                        for (const auto& path : missing) {
                            const EntryResult state = service->ObserveChild(watch, path);
                            if (state == EntryResult::Missing || state == EntryResult::Other) {
                                service->RemoveChild(watch, path);
                            }
                        }
                    }
                } catch (...) {
                    std::fputs("[Foundation][System] Failed to reconcile filesystem changes.\n", stderr);
                }
            }
        }

        static void Changed(uv_fs_event_t* native, const char* filename, int /*flags*/, int status) {
            Handle* handle = static_cast<Handle*>(native->data);
            Watch* watch = handle->Owner;
            if (watch->Closing) {
                return;
            }
            try {
                if (handle->Parent) {
                    if (!filename || status < 0) {
                        watch->RescanParent = true;
                    } else {
                        const std::filesystem::path relative = NativePath(filename).lexically_normal();
                        if (!relative.empty() && relative != "." && relative != ".." &&
                            !relative.is_absolute() && !relative.has_parent_path()) {
                            watch->ParentCandidates.insert(Utf8Path(NativePath(watch->ParentPath) / relative));
                        }
                    }
                } else if (!filename || status < 0) {
                    watch->RescanChildren = true;
                } else {
                    const std::filesystem::path relative = NativePath(filename).lexically_normal();
                    if (relative.empty() || relative == "." || relative == ".." ||
                        relative.is_absolute() || relative.has_parent_path()) {
                        return;
                    }
                    watch->DirtyChildren.insert(Utf8Path(NativePath(watch->Path) / relative));
                }
                State* service = watch->Service;
                service->DirtyWatches.insert(watch->ID);
                if (!uv_is_active(reinterpret_cast<uv_handle_t*>(&service->BatchTimer))) {
                    uv_timer_start(&service->BatchTimer, &FlushChanges, 30, 0);
                }
            } catch (...) {
                std::fputs("[Foundation][System] Failed to queue a filesystem change.\n", stderr);
            }
        }

        bool StartHandle(Watch* watch, Handle& handle, bool parent, std::string& error) {
            handle.Owner = watch;
            handle.Parent = parent;
            int result = uv_fs_event_init(&Loop, &handle.Native);
            if (result == 0) {
                handle.Initialized = true;
                ++watch->OpenHandles;
                handle.Native.data = &handle;
                const std::string& path = parent ? watch->ParentPath : watch->Path;
                result = uv_fs_event_start(&handle.Native, &Changed, path.c_str(), 0);
            }
            if (result != 0) {
                error = uv_strerror(result);
                return false;
            }
            return true;
        }

        bool Register(Command& command) {
            Watch* watch = new Watch();
            watch->Service = this;
            watch->ID = command.ID;
            try {
                if (ResolvePath(command.Path, watch->Path) != EntryResult::Present) {
                    command.Error = "Cannot resolve the filesystem watcher target.";
                    delete watch;
                    return false;
                }
                watch->ParentPath = Utf8Path(NativePath(watch->Path).parent_path());
                if (ReadEntry(watch->Path, watch->Self) != EntryResult::Present) {
                    command.Error = "The target must be an accessible regular file or directory.";
                    delete watch;
                    return false;
                }
                if (!StartHandle(watch, watch->ParentHandle, true, command.Error) ||
                    (watch->Self.Directory &&
                     (!StartHandle(watch, watch->ContentsHandle, false, command.Error) ||
                      !ScanChildren(watch->Path, watch->Children)))) {
                    if (command.Error.empty()) {
                        command.Error = "Cannot read the initial directory state.";
                    }
                    CloseWatch(watch);
                    return false;
                }
                for (const auto& [path, entry] : watch->Children) {
                    watch->PathsByIdentity[entry.Identity].insert(path);
                }
                Watches.emplace(watch->ID, watch);
                if (watch->Self.Directory) {
                    DirectoryWatches[watch->Path].insert(watch->ID);
                }
                return true;
            } catch (...) {
                CloseWatch(watch);
                throw;
            }
        }

        static void CommandsReady(uv_async_t* handle) {
            State* service = static_cast<State*>(handle->data);
            Command* command = service->PendingCommand.exchange(nullptr, std::memory_order_acquire);
            if (command) {
                try {
                    command->Success = service->Register(*command);
                } catch (...) {
                    command->Success = false;
                    // Never let exceptions cross libuv's C callback boundary.
                    try { command->Error = "Failed to allocate filesystem watcher state."; }
                    catch (...) {}
                }
                command->Done.store(true, std::memory_order_release);
                command->Done.notify_one();
            }
            if (service->StopRequested.load()) {
                while (!service->Watches.empty()) {
                    service->CloseWatch(service->Watches.begin()->second);
                }
                uv_timer_stop(&service->BatchTimer);
                uv_close(reinterpret_cast<uv_handle_t*>(&service->BatchTimer), nullptr);
                service->DirtyWatches.clear();
                uv_close(reinterpret_cast<uv_handle_t*>(&service->Wakeup), nullptr);
            }
        }

        bool Start(std::string& error) {
            int result = uv_loop_init(&Loop);
            if (result != 0) {
                error = uv_strerror(result);
                return false;
            }
            result = uv_async_init(&Loop, &Wakeup, &CommandsReady);
            if (result != 0) {
                error = uv_strerror(result);
                uv_loop_close(&Loop);
                return false;
            }
            Wakeup.data = this;
            result = uv_timer_init(&Loop, &BatchTimer);
            if (result != 0) {
                error = uv_strerror(result);
                uv_close(reinterpret_cast<uv_handle_t*>(&Wakeup), nullptr);
                uv_run(&Loop, UV_RUN_DEFAULT);
                uv_loop_close(&Loop);
                return false;
            }
            BatchTimer.data = this;
            try {
                Worker = std::thread([this]() { uv_run(&Loop, UV_RUN_DEFAULT); });
            } catch (...) {
                uv_close(reinterpret_cast<uv_handle_t*>(&BatchTimer), nullptr);
                uv_close(reinterpret_cast<uv_handle_t*>(&Wakeup), nullptr);
                uv_run(&Loop, UV_RUN_DEFAULT);
                uv_loop_close(&Loop);
                throw;
            }
            Started = true;
            return true;
        }

        void Submit(Command& command) {
            PendingCommand.store(&command, std::memory_order_release);
            uv_async_send(&Wakeup);
            // Keep bool registration synchronous without a mutex or busy wait.
            command.Done.wait(false, std::memory_order_acquire);
        }

        bool Add(const std::string& path, void* context,
                 fOnWatcherCallback callback, std::string& error) {
            error.clear();
            if (OwnerThread != std::this_thread::get_id() || Stopped || !callback ||
                path.empty() || path.find('\0') != std::string::npos) {
                error = "Register on the owner thread with a valid path and callback before shutdown.";
                return false;
            }
            if (!Started && !Start(error)) {
                return false;
            }
            Command& command = RegistrationCommand;
            command.ID = ++NextID;
            command.Path = path;
            command.Success = false;
            command.Error.clear();
            command.Done.store(false, std::memory_order_relaxed);
            Registrations.emplace(command.ID, Registration{context, std::move(callback)});
            try {
                Submit(command);
            } catch (...) {
                Registrations.erase(command.ID);
                throw;
            }
            if (!command.Success) {
                Registrations.erase(command.ID);
                error = command.Error;
            }
            return command.Success;
        }

        void Dispatch() {
            if (OwnerThread != std::this_thread::get_id() || Stopped || Dispatching) {
                return;
            }
            Dispatching = true;
            struct Guard {
                State* Service;
                ~Guard() {
                    Service->Dispatching = false;
                    if (Service->Stopped) {
                        Service->Registrations.clear();
                    }
                }
            } guard{this};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
            while (!Stopped) {
                Event* queued = nullptr;
                if (!Events.Pull(queued)) {
                    break;
                }
                Event event = std::move(*queued);
                delete queued;
                const auto found = Registrations.find(event.ID);
                if (found == Registrations.end()) {
                    continue;
                }
                if (event.Notify) {
                    Registration& registration = found->second;
                    try {
                        registration.Callback(event.Data, event.Extension, registration.Context);
                    } catch (...) {
                        std::fputs("[Foundation][System] Filesystem watcher callback threw.\n", stderr);
                    }
                }
                if (event.Terminal) {
                    Registrations.erase(found);
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    break;
                }
            }
        }

        void Stop() {
            // The owner must stop the producer before releasing callback contexts.
            // No user callback is ever executed by Worker or during Stop().
            if (Stopped) {
                return;
            }
            FoundationAssert(OwnerThread == std::this_thread::get_id(),
                             "Stop filesystem watchers on their owner thread");
            if (OwnerThread != std::this_thread::get_id()) {
                return;
            }
            Stopped = true;
            if (!Dispatching) {
                Registrations.clear();
            }
            if (Started) {
                // Shutdown must not allocate: destructors also take this path.
                StopRequested.store(true);
                uv_async_send(&Wakeup);
                Worker.join();
                uv_loop_close(&Loop);
                Started = false;
            }
            Event* event = nullptr;
            while (Events.Pull(event)) {
                delete event;
            }
        }
    };

    SystemFileWatchers::SystemFileWatchers() : _State(new State()) {}

    SystemFileWatchers::~SystemFileWatchers() {
        delete _State;
    }

    bool SystemFileWatchers::Add(const std::string& path, void* context,
                                 fOnWatcherCallback callback, std::string& error) {
        return _State->Add(path, context, std::move(callback), error);
    }

    void SystemFileWatchers::Dispatch() {
        _State->Dispatch();
    }

    void SystemFileWatchers::Stop() {
        _State->Stop();
    }
}

#ifndef TASK_API_H
#define TASK_API_H

#include "MultiSys.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace Foundation {
    inline constexpr UInt64 InvalidTaskId = 0;
    inline constexpr UInt64 FoundationTaskOwnerId = 0;
    inline constexpr UInt64 InvalidTaskOwnerId = static_cast<UInt64>(-1);

    enum class eTaskExecutionType : int {
        Concurrent,
        Serialized
    };

    enum class eTaskSubmitStatus : int {
        Accepted,
        InvalidRequest,
        OwnerUnavailable,
        TaskSystemUnavailable
    };

    struct TaskSubmitOptions final {
        UInt64 _OwnerId = InvalidTaskOwnerId;
        eTaskExecutionType _ExecutionType = eTaskExecutionType::Concurrent;
        UInt64 _SerialKey = 0;
    };

    struct TaskSubmitResult final {
        eTaskSubmitStatus _Status = eTaskSubmitStatus::InvalidRequest;
        UInt64 _TaskId = InvalidTaskId;

        bool Accepted() const {
            return _Status == eTaskSubmitStatus::Accepted && _TaskId != InvalidTaskId;
        }
    };

    struct TaskCompletion final {
        UInt64 _TaskId = InvalidTaskId;
        Int64 _Code = -1;
    };

    using fTaskOwnedContextRelease = std::function<void(void*)>;

    class ITaskContext {
    public:
        virtual ~ITaskContext() {}

        template<typename T>
        T* Get() const {
            return static_cast<T*>(GetContext());
        }

        template<typename T>
        bool SetOwned(T* const context) {
            return SetOwnedContext(context, [](void* const value) {
                delete static_cast<T*>(value);
            });
        }

        virtual UInt64 GetTaskId() const = 0;
        virtual bool IsCancellationRequested() const = 0;
        virtual void* GetContext() const = 0;
        virtual bool SetOwnedContext(void* context, fTaskOwnedContextRelease release) = 0;
    };

    // The returned bool is the only success value. Code carries details and
    // is initialized to -1 by the Task runtime before the worker is invoked.
    using fTask = std::function<bool(OUT Int64& code, ITaskContext&)>;
    using fTaskCompleted = std::function<void(
        const TaskCompletion&,
        bool succeed,
        const ITaskContext&
    )>;
    using fTasksAllCompleted = std::function<void(
        bool succeed,
        int succeedCount,
        int failedCount
    )>;

    enum class eTaskGroupStartStatus : int {
        Accepted,
        AlreadyStarted,
        InvalidRequest,
        OwnerUnavailable,
        TaskSystemUnavailable
    };

    struct TaskGroupStartResult final {
        eTaskGroupStartStatus _Status = eTaskGroupStartStatus::InvalidRequest;
        std::vector<UInt64> _TaskIds;

        bool Accepted() const {
            return _Status == eTaskGroupStartStatus::Accepted;
        }

        std::size_t TaskCount() const {
            return _TaskIds.size();
        }
    };

    class ITaskGroup {
    public:
        virtual ~ITaskGroup() {}

        // A TaskGroup is a one-shot builder. Tasks and the final callback may
        // only be changed before a successful Start().
        virtual bool AddTask(fTask task, fTaskCompleted callback) = 0;
        virtual bool SetTasksAllCompletedCallback(fTasksAllCompleted callback) = 0;

        // The whole group is accepted or rejected atomically. Concurrent runs
        // the tasks in parallel; Serialized preserves their insertion order
        // under options._SerialKey. Individual callbacks run first, followed
        // by the all-completed callback, through FoundationApi::Update().
        //
        // After a successful Start(), this group is runtime-owned and is
        // automatically destroyed after its all-completed phase. The caller
        // must not access the pointer again.
        virtual TaskGroupStartResult Start(const TaskSubmitOptions& options) = 0;
    };

    class ITaskApi {
    public:
        virtual ~ITaskApi() {}

        // Owner 只代表完成回调的有效期，不拥有调用方对象。调用方必须在 Release 前注销。
        virtual UInt64 RegisterTaskOwner() = 0;
        // 非阻塞：拒绝该 Owner 的新任务，请求取消已有任务，并抑制后续完成回调。
        virtual void UnregisterTaskOwner(UInt64 ownerId) = 0;

        virtual TaskSubmitResult SubmitTask(
            const TaskSubmitOptions& options,
            fTask task,
            fTaskCompleted callback
        ) = 0;
        virtual bool CancelTask(UInt64 taskId) = 0;

        // Returns a one-shot group owned by the Task runtime. An unstarted
        // group is reclaimed automatically when Foundation is released.
        // This default keeps source compatibility with lightweight test
        // implementations that only provide single-Task behavior.
        virtual ITaskGroup* CreateTaskGroup() {
            return nullptr;
        }
    };
}

#endif // TASK_API_H

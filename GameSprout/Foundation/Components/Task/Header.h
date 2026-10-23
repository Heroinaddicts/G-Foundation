#ifndef TASK_HEADER_H
#define TASK_HEADER_H

#include "ITaskManager.h"
#include "SafeSystem.hpp"
#include "SafeThread.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Foundation {
    class TaskGroup;

    class TaskContext final : public ITaskContext {
    public:
        TaskContext(UInt64 taskId, const std::atomic_bool* cancellationRequested);
        ~TaskContext() override;

        TaskContext(const TaskContext&) = delete;
        TaskContext& operator=(const TaskContext&) = delete;

        UInt64 GetTaskId() const override;
        bool IsCancellationRequested() const override;
        void* GetContext() const override;
        bool SetOwnedContext(void* context, fTaskOwnedContextRelease release) override;

    private:
        UInt64 _TaskId = InvalidTaskId;
        const std::atomic_bool* _CancellationRequested = nullptr;
        void* _Context = nullptr;
        fTaskOwnedContextRelease _ContextRelease;
    };

    struct TaskInfo final {
        TaskInfo(
            UInt64 taskId,
            UInt64 ownerId,
            eTaskExecutionType executionType,
            UInt64 serialKey,
            fTask task,
            fTaskCompleted callback,
            TaskGroup* taskGroup = nullptr
        );

        TaskInfo(const TaskInfo&) = delete;
        TaskInfo& operator=(const TaskInfo&) = delete;

        UInt64 _TaskId = InvalidTaskId;
        UInt64 _OwnerId = InvalidTaskOwnerId;
        eTaskExecutionType _ExecutionType = eTaskExecutionType::Concurrent;
        UInt64 _SerialKey = 0;
        fTask _Task;
        fTaskCompleted _TaskCompleted;
        TaskGroup* _TaskGroup = nullptr;
        std::atomic_bool _CancellationRequested = false;
        bool _Succeeded = false;
        TaskCompletion _Completion;
        TaskContext _Context;
    };
}

#endif // TASK_HEADER_H

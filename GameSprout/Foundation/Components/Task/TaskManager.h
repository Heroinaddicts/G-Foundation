#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "Header.h"

namespace Foundation {
    class TaskGroup;

    class TaskManager final : public ITaskManager, private FoundationUtils::SafeThread {
    public:
        ~TaskManager() override {}

        bool Initialize(FoundationApi* foundation) override;
        bool Launch(FoundationApi* foundation) override;
        void Release(FoundationApi* foundation) override;
        void EarlyUpdate(FoundationApi* foundation) override;
        void Update(FoundationApi* foundation) override;
        void LaterUpdate(FoundationApi* foundation) override;

        UInt64 RegisterTaskOwner() override;
        void UnregisterTaskOwner(UInt64 ownerId) override;
        TaskSubmitResult SubmitTask(
            const TaskSubmitOptions& options,
            fTask task,
            fTaskCompleted callback
        ) override;
        bool CancelTask(UInt64 taskId) override;
        ITaskGroup* CreateTaskGroup() override;
        UInt64 GetCompletionBudgetMicroseconds() const override;
        bool SetCompletionBudgetMicroseconds(UInt64 budget) override;

    private:
        friend class TaskGroup;

        void Run(int threadIndex, void* context) override;
        bool HasPendingTaskLocked(int threadIndex) const;
        TaskInfo* TakePendingTaskLocked(int threadIndex);
        bool IsOwnerActiveLocked(UInt64 ownerId) const;
        void CompleteTask(TaskInfo* task, bool invokeCallback);
        void CancelAllTasksLocked();
        TaskGroupStartResult StartTaskGroup(TaskGroup* group, const TaskSubmitOptions& options);
        void FinalizeTaskGroup(
            TaskGroup* group,
            fTasksAllCompleted callback,
            bool invokeCallback,
            int succeedCount,
            int failedCount
        );

        std::mutex _Mutex;
        std::condition_variable _WakeCondition;
        std::deque<TaskInfo*> _ConcurrentTasks;
        std::vector<std::deque<TaskInfo*>> _SerializedTasks;
        std::deque<TaskInfo*> _CompletedTasks;
        std::deque<TaskGroup*> _CompletedTaskGroups;
        std::unordered_map<UInt64, TaskInfo*> _Tasks;
        std::unordered_set<TaskGroup*> _TaskGroups;
        std::unordered_set<UInt64> _Owners;
        UInt64 _NextTaskId = 1;
        UInt64 _NextOwnerId = 1;
        int _WorkerCount = 0;
        bool _AcceptingTasks = false;
        FoundationApi* _Foundation = nullptr;
        // Only the Foundation Update thread reads or changes this budget.
        UInt64 _CompletionBudgetMicroseconds = 1000;
    };
}

#endif // TASK_MANAGER_H

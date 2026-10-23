#ifndef TASK_GROUP_H
#define TASK_GROUP_H

#include "Header.h"

#include <mutex>
#include <vector>

namespace Foundation {
    class TaskManager;

    class TaskGroup final : public ITaskGroup {
    public:
        explicit TaskGroup(TaskManager* manager);
        ~TaskGroup() override = default;

        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;

        bool AddTask(fTask task, fTaskCompleted callback) override;
        bool SetTasksAllCompletedCallback(fTasksAllCompleted callback) override;
        TaskGroupStartResult Start(const TaskSubmitOptions& options) override;

    private:
        friend class TaskManager;

        struct Entry final {
            fTask _Task;
            fTaskCompleted _Completed;
        };

        void CompleteTask(bool succeed, bool invokeCallback);
        void CompleteEmpty(bool invokeCallback);

        TaskManager* _Manager = nullptr;
        std::mutex _Mutex;
        std::vector<Entry> _Entries;
        fTasksAllCompleted _AllCompleted;
        UInt64 _OwnerId = InvalidTaskOwnerId;
        std::size_t _RemainingTaskCount = 0;
        int _SucceedCount = 0;
        int _FailedCount = 0;
        bool _Started = false;
        bool _CallbacksEnabled = true;
    };
}

#endif // TASK_GROUP_H

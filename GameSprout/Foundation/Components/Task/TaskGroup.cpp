#include "TaskGroup.h"

#include "TaskManager.h"

#include <utility>

namespace Foundation {
    TaskGroup::TaskGroup(TaskManager* const manager)
        : _Manager(manager) {
    }

    bool TaskGroup::AddTask(fTask task, fTaskCompleted callback) {
        if (!task) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (_Started || !_Manager) {
            return false;
        }
        try {
            _Entries.push_back({std::move(task), std::move(callback)});
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool TaskGroup::SetTasksAllCompletedCallback(fTasksAllCompleted callback) {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (_Started || !_Manager) {
            return false;
        }
        _AllCompleted = std::move(callback);
        return true;
    }

    TaskGroupStartResult TaskGroup::Start(const TaskSubmitOptions& options) {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (_Started) {
            TaskGroupStartResult result;
            result._Status = eTaskGroupStartStatus::AlreadyStarted;
            return result;
        }
        if (!_Manager) {
            TaskGroupStartResult result;
            result._Status = eTaskGroupStartStatus::TaskSystemUnavailable;
            return result;
        }

        // Publish the completion state before workers can observe the queued
        // tasks. A rejected atomic submission rolls the builder back so the
        // caller may correct the options and retry.
        _Started = true;
        _OwnerId = options._OwnerId;
        _RemainingTaskCount = _Entries.size();
        _SucceedCount = 0;
        _FailedCount = 0;
        _CallbacksEnabled = true;
        TaskGroupStartResult result = _Manager->StartTaskGroup(this, options);
        if (!result.Accepted()) {
            _Started = false;
            _OwnerId = InvalidTaskOwnerId;
            _RemainingTaskCount = 0;
            _SucceedCount = 0;
            _FailedCount = 0;
            return result;
        }

        _Entries.clear();
        return result;
    }

    void TaskGroup::CompleteTask(
        const bool succeed,
        const bool invokeCallback
    ) {
        fTasksAllCompleted callback;
        TaskManager* manager = nullptr;
        int succeedCount = 0;
        int failedCount = 0;
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            if (!_Started || _RemainingTaskCount == 0) {
                return;
            }
            if (!invokeCallback) {
                _CallbacksEnabled = false;
            }
            if (succeed) {
                ++_SucceedCount;
            }
            else {
                ++_FailedCount;
            }
            --_RemainingTaskCount;
            if (_RemainingTaskCount != 0) {
                return;
            }
            manager = _Manager;
            _Manager = nullptr;
            if (_CallbacksEnabled) {
                callback = std::move(_AllCompleted);
            }
            succeedCount = _SucceedCount;
            failedCount = _FailedCount;
        }

        if (manager) {
            manager->FinalizeTaskGroup(
                this,
                std::move(callback),
                invokeCallback,
                succeedCount,
                failedCount
            );
        }
    }

    void TaskGroup::CompleteEmpty(const bool invokeCallback) {
        fTasksAllCompleted callback;
        TaskManager* manager = nullptr;
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            if (!_Started || _RemainingTaskCount != 0 || !_Manager) {
                return;
            }
            manager = _Manager;
            _Manager = nullptr;
            if (invokeCallback) {
                callback = std::move(_AllCompleted);
            }
        }

        manager->FinalizeTaskGroup(
            this,
            std::move(callback),
            invokeCallback,
            0,
            0
        );
    }
}

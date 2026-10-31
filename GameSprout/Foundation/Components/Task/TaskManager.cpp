#include "TaskManager.h"
#include "TaskGroup.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace Foundation {
    TaskContext::TaskContext(
        const UInt64 taskId,
        const std::atomic_bool* const cancellationRequested
    )
        : _TaskId(taskId)
        , _CancellationRequested(cancellationRequested) {
    }

    TaskContext::~TaskContext() {
        if (_Context && _ContextRelease) {
            try {
                _ContextRelease(_Context);
            }
            catch (...) {
            }
        }
        _Context = nullptr;
        _ContextRelease = {};
    }

    UInt64 TaskContext::GetTaskId() const {
        return _TaskId;
    }

    bool TaskContext::IsCancellationRequested() const {
        return _CancellationRequested
            && _CancellationRequested->load(std::memory_order_acquire);
    }

    void* TaskContext::GetContext() const {
        return _Context;
    }

    bool TaskContext::SetOwnedContext(
        void* const context,
        fTaskOwnedContextRelease release
    ) {
        if (_Context || !context || !release) {
            return false;
        }
        _Context = context;
        _ContextRelease = std::move(release);
        return true;
    }

    TaskInfo::TaskInfo(
        const UInt64 taskId,
        const UInt64 ownerId,
        const eTaskExecutionType executionType,
        const UInt64 serialKey,
        fTask task,
        fTaskCompleted callback,
        TaskGroup* const taskGroup
    )
        : _TaskId(taskId)
        , _OwnerId(ownerId)
        , _ExecutionType(executionType)
        , _SerialKey(serialKey)
        , _Task(std::move(task))
        , _TaskCompleted(std::move(callback))
        , _TaskGroup(taskGroup)
        , _Context(taskId, &_CancellationRequested) {
        _Completion._TaskId = taskId;
    }

    bool TaskManager::Initialize(FoundationApi* const foundation) {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (!foundation || _Foundation || RunningThreadCount() != 0) {
            return false;
        }

        _Foundation = foundation;
        _WorkerCount = FoundationUtils::SafeSystem::Hardware::NumberOfCpuCores();
        if (_WorkerCount <= 0) {
            _WorkerCount = 1;
        }
        try {
            _SerializedTasks.clear();
            _SerializedTasks.resize(static_cast<std::size_t>(_WorkerCount));
            _ConcurrentTasks.clear();
            _CompletedTasks.clear();
            _CompletedTaskGroups.clear();
            _Tasks.clear();
            _TaskGroups.clear();
            _Owners.clear();
        }
        catch (...) {
            _SerializedTasks.clear();
            _WorkerCount = 0;
            _Foundation = nullptr;
            return false;
        }
        _NextTaskId = 1;
        _NextOwnerId = 1;
        _AcceptingTasks = false;
        _CompletionBudgetMicroseconds = 1000;
        return true;
    }

    bool TaskManager::Launch(FoundationApi* const foundation) {
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            if (foundation != _Foundation || _AcceptingTasks || RunningThreadCount() != 0) {
                return false;
            }
        }

        if (!Start(_WorkerCount)) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        _AcceptingTasks = true;
        return true;
    }

    void TaskManager::Release(FoundationApi* const foundation) {
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            _AcceptingTasks = false;
            CancelAllTasksLocked();
        }
        RequestStop();
        _WakeCondition.notify_all();
        Wait();

        std::deque<TaskInfo*> completedTasks;
        std::deque<TaskGroup*> completedTaskGroups;
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            completedTasks.swap(_CompletedTasks);
            completedTaskGroups.swap(_CompletedTaskGroups);
            _ConcurrentTasks.clear();
            for (std::deque<TaskInfo*>& tasks : _SerializedTasks) {
                tasks.clear();
            }
            _SerializedTasks.clear();
            _Owners.clear();
            _Tasks.clear();
            _WorkerCount = 0;
            _Foundation = nullptr;
        }

        for (TaskInfo* const task : completedTasks) {
            CompleteTask(task, task && task->_OwnerId == FoundationTaskOwnerId);
        }
        for (TaskGroup* const group : completedTaskGroups) {
            if (group) {
                group->CompleteEmpty(group->_OwnerId == FoundationTaskOwnerId);
            }
        }

        std::vector<TaskGroup*> abandonedGroups;
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            abandonedGroups.assign(_TaskGroups.begin(), _TaskGroups.end());
            _TaskGroups.clear();
        }
        for (TaskGroup* const group : abandonedGroups) {
            delete group;
        }
    }

    void TaskManager::EarlyUpdate(FoundationApi* const foundation) {
        (void)foundation;
    }

    void TaskManager::Update(FoundationApi* const foundation) {
        if (foundation != _Foundation) {
            return;
        }

        const UInt64 beginTick = FoundationUtils::SafeSystem::Time::GetMicroSecond();
        while (true) {
            TaskInfo* task = nullptr;
            TaskGroup* completedGroup = nullptr;
            bool invokeCallback = false;
            {
                std::lock_guard<std::mutex> lock(_Mutex);
                if (!_CompletedTasks.empty()) {
                    task = _CompletedTasks.front();
                    _CompletedTasks.pop_front();
                    if (task) {
                        _Tasks.erase(task->_TaskId);
                        invokeCallback = IsOwnerActiveLocked(task->_OwnerId);
                    }
                }
                else if (!_CompletedTaskGroups.empty()) {
                    completedGroup = _CompletedTaskGroups.front();
                    _CompletedTaskGroups.pop_front();
                    if (completedGroup) {
                        invokeCallback = IsOwnerActiveLocked(completedGroup->_OwnerId);
                    }
                }
                else {
                    break;
                }
            }

            if (task) {
                CompleteTask(task, invokeCallback);
            }
            else if (completedGroup) {
                completedGroup->CompleteEmpty(invokeCallback);
            }
            if (FoundationUtils::SafeSystem::Time::GetMicroSecond() - beginTick >= _CompletionBudgetMicroseconds) {
                break;
            }
        }
    }

    UInt64 TaskManager::GetCompletionBudgetMicroseconds() const {
        return _CompletionBudgetMicroseconds;
    }

    bool TaskManager::SetCompletionBudgetMicroseconds(const UInt64 budget) {
        if (budget == 0) {
            return false;
        }
        _CompletionBudgetMicroseconds = budget;
        return true;
    }

    void TaskManager::LaterUpdate(FoundationApi* const foundation) {
        (void)foundation;
    }

    UInt64 TaskManager::RegisterTaskOwner() {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (!_AcceptingTasks) {
            return InvalidTaskOwnerId;
        }

        UInt64 ownerId = _NextOwnerId++;
        while (ownerId == InvalidTaskOwnerId || ownerId == FoundationTaskOwnerId) {
            ownerId = _NextOwnerId++;
        }
        try {
            _Owners.insert(ownerId);
            return ownerId;
        }
        catch (...) {
            return InvalidTaskOwnerId;
        }
    }

    void TaskManager::UnregisterTaskOwner(const UInt64 ownerId) {
        if (ownerId == InvalidTaskOwnerId) {
            return;
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (_Owners.erase(ownerId) == 0) {
            return;
        }
        for (const auto& entry : _Tasks) {
            TaskInfo* const task = entry.second;
            if (task && task->_OwnerId == ownerId) {
                task->_CancellationRequested.store(true, std::memory_order_release);
            }
        }
        _WakeCondition.notify_all();
    }

    TaskSubmitResult TaskManager::SubmitTask(
        const TaskSubmitOptions& options,
        fTask task,
        fTaskCompleted callback
    ) {
        TaskSubmitResult result;
        if (!task
            || (options._ExecutionType == eTaskExecutionType::Serialized
                && options._SerialKey == 0)) {
            return result;
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (!_AcceptingTasks || _WorkerCount <= 0 || _SerializedTasks.empty()) {
            result._Status = eTaskSubmitStatus::TaskSystemUnavailable;
            return result;
        }
        if (!IsOwnerActiveLocked(options._OwnerId)) {
            result._Status = eTaskSubmitStatus::OwnerUnavailable;
            return result;
        }

        UInt64 taskId = _NextTaskId++;
        if (taskId == InvalidTaskId) {
            taskId = _NextTaskId++;
        }

        TaskInfo* info = nullptr;
        try {
            info = txnew TaskInfo(
                taskId,
                options._OwnerId,
                options._ExecutionType,
                options._SerialKey,
                std::move(task),
                std::move(callback)
            );
            _Tasks.emplace(taskId, info);
            if (options._ExecutionType == eTaskExecutionType::Serialized) {
                const std::size_t index = static_cast<std::size_t>(
                    options._SerialKey % static_cast<UInt64>(_WorkerCount)
                );
                _SerializedTasks[index].push_back(info);
            }
            else {
                _ConcurrentTasks.push_back(info);
            }
        }
        catch (...) {
            if (info) {
                _Tasks.erase(taskId);
                txdel info;
            }
            result._Status = eTaskSubmitStatus::TaskSystemUnavailable;
            return result;
        }

        result._Status = eTaskSubmitStatus::Accepted;
        result._TaskId = taskId;
        _WakeCondition.notify_all();
        return result;
    }

    bool TaskManager::CancelTask(const UInt64 taskId) {
        if (taskId == InvalidTaskId) {
            return false;
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        const auto iterator = _Tasks.find(taskId);
        if (iterator == _Tasks.end() || !iterator->second) {
            return false;
        }
        iterator->second->_CancellationRequested.store(true, std::memory_order_release);
        _WakeCondition.notify_all();
        return true;
    }

    ITaskGroup* TaskManager::CreateTaskGroup() {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (!_AcceptingTasks) {
            return nullptr;
        }

        TaskGroup* group = nullptr;
        try {
            group = txnew TaskGroup(this);
            _TaskGroups.insert(group);
            return group;
        }
        catch (...) {
            delete group;
            return nullptr;
        }
    }

    void TaskManager::Run(const int threadIndex, void* const context) {
        (void)context;
        while (true) {
            TaskInfo* task = nullptr;
            {
                std::unique_lock<std::mutex> lock(_Mutex);
                _WakeCondition.wait(lock, [this, threadIndex]() {
                    return IsStopRequested() || HasPendingTaskLocked(threadIndex);
                });
                task = TakePendingTaskLocked(threadIndex);
                if (!task && IsStopRequested()) {
                    return;
                }
            }

            if (!task) {
                continue;
            }

            if (task->_CancellationRequested.load(std::memory_order_acquire)) {
                task->_Completion._Code = -1;
                task->_Succeeded = false;
            }
            else {
                try {
                    task->_Completion._Code = -1;
                    const bool succeed = task->_Task(
                        task->_Completion._Code,
                        task->_Context
                    );
                    task->_Succeeded = succeed
                        && !task->_CancellationRequested.load(std::memory_order_acquire);
                }
                catch (...) {
                    task->_Completion._Code = -1;
                    task->_Succeeded = false;
                }
            }

            bool completionQueued = false;
            {
                std::lock_guard<std::mutex> lock(_Mutex);
                try {
                    _CompletedTasks.push_back(task);
                    completionQueued = true;
                }
                catch (...) {
                    _Tasks.erase(task->_TaskId);
                }
            }
            if (!completionQueued) {
                CompleteTask(task, false);
            }
        }
    }

    bool TaskManager::HasPendingTaskLocked(const int threadIndex) const {
        if (threadIndex >= 0
            && static_cast<std::size_t>(threadIndex) < _SerializedTasks.size()
            && !_SerializedTasks[static_cast<std::size_t>(threadIndex)].empty()) {
            return true;
        }
        return !_ConcurrentTasks.empty();
    }

    TaskInfo* TaskManager::TakePendingTaskLocked(const int threadIndex) {
        if (threadIndex >= 0
            && static_cast<std::size_t>(threadIndex) < _SerializedTasks.size()) {
            std::deque<TaskInfo*>& serializedTasks = _SerializedTasks[static_cast<std::size_t>(threadIndex)];
            if (!serializedTasks.empty()) {
                TaskInfo* const task = serializedTasks.front();
                serializedTasks.pop_front();
                return task;
            }
        }
        if (_ConcurrentTasks.empty()) {
            return nullptr;
        }
        TaskInfo* const task = _ConcurrentTasks.front();
        _ConcurrentTasks.pop_front();
        return task;
    }

    bool TaskManager::IsOwnerActiveLocked(const UInt64 ownerId) const {
        return ownerId == FoundationTaskOwnerId || _Owners.find(ownerId) != _Owners.end();
    }

    void TaskManager::CompleteTask(TaskInfo* const task, const bool invokeCallback) {
        if (!task) {
            return;
        }
        if (invokeCallback && task->_TaskCompleted) {
            try {
                task->_TaskCompleted(
                    task->_Completion,
                    task->_Succeeded,
                    task->_Context
                );
            }
            catch (...) {
            }
        }
        TaskGroup* const group = task->_TaskGroup;
        if (group) {
            group->CompleteTask(task->_Succeeded, invokeCallback);
        }
        txdel task;
    }

    void TaskManager::CancelAllTasksLocked() {
        for (const auto& entry : _Tasks) {
            if (entry.second) {
                entry.second->_CancellationRequested.store(true, std::memory_order_release);
            }
        }
    }

    TaskGroupStartResult TaskManager::StartTaskGroup(
        TaskGroup* const group,
        const TaskSubmitOptions& options
    ) {
        TaskGroupStartResult result;
        if (!group
            || (options._ExecutionType == eTaskExecutionType::Serialized
                && options._SerialKey == 0)) {
            result._Status = eTaskGroupStartStatus::InvalidRequest;
            return result;
        }

        std::lock_guard<std::mutex> lock(_Mutex);
        if (!_AcceptingTasks || _WorkerCount <= 0 || _SerializedTasks.empty()
            || _TaskGroups.find(group) == _TaskGroups.end()) {
            result._Status = eTaskGroupStartStatus::TaskSystemUnavailable;
            return result;
        }
        if (!IsOwnerActiveLocked(options._OwnerId)) {
            result._Status = eTaskGroupStartStatus::OwnerUnavailable;
            return result;
        }

        if (group->_Entries.empty()) {
            try {
                _CompletedTaskGroups.push_back(group);
            }
            catch (...) {
                result._Status = eTaskGroupStartStatus::TaskSystemUnavailable;
                return result;
            }
            result._Status = eTaskGroupStartStatus::Accepted;
            return result;
        }

        std::vector<TaskInfo*> tasks;
        try {
            tasks.reserve(group->_Entries.size());
            result._TaskIds.reserve(group->_Entries.size());
            for (const TaskGroup::Entry& entry : group->_Entries) {
                UInt64 taskId = _NextTaskId++;
                while (taskId == InvalidTaskId || _Tasks.find(taskId) != _Tasks.end()) {
                    taskId = _NextTaskId++;
                }
                TaskInfo* const task = txnew TaskInfo(
                    taskId,
                    options._OwnerId,
                    options._ExecutionType,
                    options._SerialKey,
                    entry._Task,
                    entry._Completed,
                    group
                );
                try {
                    tasks.push_back(task);
                }
                catch (...) {
                    txdel task;
                    throw;
                }
                result._TaskIds.push_back(taskId);
            }

            _Tasks.reserve(_Tasks.size() + tasks.size());
            for (TaskInfo* const task : tasks) {
                _Tasks.emplace(task->_TaskId, task);
                if (options._ExecutionType == eTaskExecutionType::Serialized) {
                    const std::size_t index = static_cast<std::size_t>(
                        options._SerialKey % static_cast<UInt64>(_WorkerCount)
                    );
                    _SerializedTasks[index].push_back(task);
                }
                else {
                    _ConcurrentTasks.push_back(task);
                }
            }
        }
        catch (...) {
            for (std::deque<TaskInfo*>& queue : _SerializedTasks) {
                for (TaskInfo* const task : tasks) {
                    queue.erase(std::remove(queue.begin(), queue.end(), task), queue.end());
                }
            }
            for (TaskInfo* const task : tasks) {
                _ConcurrentTasks.erase(
                    std::remove(_ConcurrentTasks.begin(), _ConcurrentTasks.end(), task),
                    _ConcurrentTasks.end()
                );
                _Tasks.erase(task->_TaskId);
                txdel task;
            }
            result._TaskIds.clear();
            result._Status = eTaskGroupStartStatus::TaskSystemUnavailable;
            return result;
        }

        result._Status = eTaskGroupStartStatus::Accepted;
        _WakeCondition.notify_all();
        return result;
    }

    void TaskManager::FinalizeTaskGroup(
        TaskGroup* const group,
        fTasksAllCompleted callback,
        const bool invokeCallback,
        const int succeedCount,
        const int failedCount
    ) {
        if (!group) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_Mutex);
            _TaskGroups.erase(group);
        }
        if (invokeCallback && callback) {
            try {
                callback(failedCount == 0, succeedCount, failedCount);
            }
            catch (...) {
            }
        }
        delete group;
    }
}

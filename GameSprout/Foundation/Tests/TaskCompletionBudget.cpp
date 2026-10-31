#include "../Components/Task/TaskManager.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// TaskManager only needs this object's identity. No other service is started.
class TestFoundation final : public Foundation::FoundationApi {
public:
    bool Initialize(int, char**) override { return true; }
    bool Launch() override { return true; }
    void Release() override {}
    const char* GetLaunchArg(const std::string&) const override { return nullptr; }
    Foundation::INetApi* NetApi() const override { return nullptr; }
    Foundation::ISshApi* SshApi() const override { return nullptr; }
    Foundation::ISystemApi* SystemApi() const override { return nullptr; }
    Foundation::ITimerApi* TimerApi() const override { return nullptr; }
    Foundation::IHttpApi* HttpApi() const override { return nullptr; }
    Foundation::IMemoryApi* MemoryApi() const override { return nullptr; }
    Foundation::IMysqlApi* MysqlApi() const override { return nullptr; }
    Foundation::IRedisApi* RedisApi() const override { return nullptr; }
    Foundation::ITaskApi* TaskApi() const override { return nullptr; }
    Foundation::IWebSocketsApi* WebSocketsApi() const override { return nullptr; }
    void Update() override {}
    void RegisterLogEvent(fLogEvent) override {}
    void SetLogTagFunction(fGetLogTagByLevel) override {}
    bool SetLogInfo(const std::string&, const std::string&) override { return true; }
    void LogAsync(int, const char*, bool, const char*, int) override {}
    void LogSync(int, const char*, bool, const char*, int) override {}
};
}

int main() {
    TestFoundation foundation;
    Foundation::TaskManager tasks;
    try {
        Check(tasks.Initialize(&foundation), "Initialize failed");
        Check(tasks.Launch(&foundation), "Launch failed");
        Check(tasks.GetCompletionBudgetMicroseconds() == 1000, "Default budget changed");
        Check(!tasks.SetCompletionBudgetMicroseconds(0), "Zero budget must fail");
        Check(tasks.GetCompletionBudgetMicroseconds() == 1000, "Invalid budget changed state");

        constexpr int taskCount = 12;
        std::atomic<int> worked = 0;
        int completed = 0;
        const std::thread::id mainThread = std::this_thread::get_id();
        Foundation::TaskSubmitOptions options;
        options._OwnerId = tasks.RegisterTaskOwner();
        for (int index = 0; index < taskCount; ++index) {
            const auto submitted = tasks.SubmitTask(options,
                [&worked](Int64& code, Foundation::ITaskContext&) {
                    ++worked;
                    code = 0;
                    return true;
                },
                [&](const Foundation::TaskCompletion&, bool success, const Foundation::ITaskContext&) {
                    Check(success, "Worker failed");
                    Check(std::this_thread::get_id() == mainThread, "Completion moved off Update thread");
                    ++completed;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                });
            Check(submitted.Accepted(), "Submission failed");
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (worked != taskCount && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(worked == taskCount, "Workers did not finish");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(completed == 0, "Callback ran before Update");

        tasks.Update(&foundation);
        Check(completed == 1, "Default budget did not stop after a slow callback");
        Check(tasks.SetCompletionBudgetMicroseconds(1000000), "Custom budget rejected");
        tasks.Update(&foundation);
        Check(completed == taskCount, "Expanded budget did not drain ready completions");

        Check(tasks.SetCompletionBudgetMicroseconds(1000), "Budget restoration failed");
        const auto group = tasks.CreateTaskGroup();
        Check(group != nullptr, "TaskGroup creation failed");
        int groupCompleted = 0;
        for (int index = 0; index < 2; ++index) {
            Check(group->AddTask([](Int64& code, Foundation::ITaskContext&) { code = 0; return true; },
                [&](const Foundation::TaskCompletion&, bool success, const Foundation::ITaskContext&) {
                    Check(success && std::this_thread::get_id() == mainThread, "Group completion thread changed");
                    ++groupCompleted;
                }), "Group task rejected");
        }
        Check(group->Start(options).Accepted(), "TaskGroup start failed");
        while (groupCompleted != 2 && std::chrono::steady_clock::now() < deadline) {
            tasks.Update(&foundation);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(groupCompleted == 2, "Group completion did not drain after restoring budget");
        tasks.UnregisterTaskOwner(options._OwnerId);
        tasks.Release(&foundation);
        std::cout << "Task completion budgets and Update-thread dispatch passed\n";
        return 0;
    } catch (const std::exception& error) {
        tasks.Release(&foundation);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

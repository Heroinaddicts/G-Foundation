#ifndef FOUNDATION_UTILS_SAFE_THREAD_HPP
#define FOUNDATION_UTILS_SAFE_THREAD_HPP

#include "MultiSys.hpp"
#include "XLock.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace FoundationUtils {
    class SafeThread {
    public:
        SafeThread() = default;
        virtual ~SafeThread();

        SafeThread(const SafeThread&) = delete;
        SafeThread& operator=(const SafeThread&) = delete;

        // The borrowed context must remain valid until Wait() returns.
        bool Start(const int threadCount = 1, void* const context = nullptr);
        void RequestStop();
        // A derived owner must call RequestStop() and Wait() before its members are destroyed.
        // Wait() must not be called from a worker owned by this instance.
        void Wait();
        int RunningThreadCount() const;

    protected:
        virtual void Run(const int threadIndex, void* const context) = 0;

        bool IsStopRequested() const;

    private:
        static void RunOwnedThread(SafeThread* const owner, const int threadIndex, void* const context);

        std::atomic<bool> _StopRequested = true;
        std::atomic<int> _RunningThreadCount = 0;
        SpinLock _Lock;
        std::vector<std::thread> _Threads;
    };
}

#if defined(FOUNDATION_UTILS_SAFE_THREAD_BUILD)
namespace FoundationUtils {
    SafeThread::~SafeThread() {
        FoundationAssert(_Threads.empty() && _RunningThreadCount.load(std::memory_order_acquire) == 0, "SafeThread must be stopped and waited before destruction");
    }

    void SafeThread::RunOwnedThread(SafeThread* const owner, const int threadIndex, void* const context) {
        FoundationAssert(owner, "SafeThread owner is null");
        if (!owner) {
            return;
        }

        try {
            owner->Run(threadIndex, context);
        }
        catch (...) {
            owner->_RunningThreadCount.fetch_sub(1, std::memory_order_acq_rel);
            FoundationAssert(false, "Unhandled exception escaped from SafeThread::Run");
            return;
        }
        owner->_RunningThreadCount.fetch_sub(1, std::memory_order_acq_rel);
    }

    bool SafeThread::Start(const int threadCount, void* const context) {
        if (threadCount <= 0) {
            return false;
        }

        AUTO_LOCK(_Lock);
        if (!_Threads.empty() || _RunningThreadCount.load(std::memory_order_acquire) != 0) {
            return false;
        }

        _StopRequested.store(false, std::memory_order_release);
        try {
            _Threads.reserve(static_cast<std::size_t>(threadCount));
        }
        catch (...) {
            _StopRequested.store(true, std::memory_order_release);
            return false;
        }

        for (int index = 0; index < threadCount; ++index) {
            _RunningThreadCount.fetch_add(1, std::memory_order_acq_rel);
            try {
                _Threads.emplace_back(&SafeThread::RunOwnedThread, this, index, context);
            }
            catch (...) {
                _RunningThreadCount.fetch_sub(1, std::memory_order_acq_rel);
                _StopRequested.store(true, std::memory_order_release);
                for (std::thread& thread : _Threads) {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
                _Threads.clear();
                return false;
            }
        }
        return true;
    }

    void SafeThread::RequestStop() {
        _StopRequested.store(true, std::memory_order_release);
    }

    void SafeThread::Wait() {
        AUTO_LOCK(_Lock);
        const std::thread::id currentThreadID = std::this_thread::get_id();
        for (std::thread& thread : _Threads) {
            FoundationAssert(thread.get_id() != currentThreadID, "SafeThread::Wait cannot be called by its worker thread");
            if (thread.get_id() == currentThreadID) {
                return;
            }
        }

        for (std::thread& thread : _Threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        _Threads.clear();
        FoundationAssert(_RunningThreadCount.load(std::memory_order_acquire) == 0, "SafeThread workers did not exit before Wait returned");
    }

    int SafeThread::RunningThreadCount() const {
        return _RunningThreadCount.load(std::memory_order_acquire);
    }

    bool SafeThread::IsStopRequested() const {
        return _StopRequested.load(std::memory_order_acquire);
    }
}
#endif // defined(FOUNDATION_UTILS_SAFE_THREAD_BUILD)

#endif // FOUNDATION_UTILS_SAFE_THREAD_HPP



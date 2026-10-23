#ifndef FOUNDATION_UTILS_X_LOCK_HPP
#define FOUNDATION_UTILS_X_LOCK_HPP

#include "MultiSys.hpp"
#include "SafeSystem.hpp"
#include <atomic>
#include <string>

namespace FoundationUtils {
    class ILock {
    public:
        virtual ~ILock() {}

        virtual bool TryLock() = 0;

        virtual void Lock(const char* file, const int line) = 0;
        virtual void Unlock() = 0;
        virtual void ForceUnlock() = 0;
    };

#define FOUNDATION_LOCK_NAME_DETAIL(line) lunokiLock##line
#define FOUNDATION_LOCK_NAME(line) FOUNDATION_LOCK_NAME_DETAIL(line)
#define AUTO_LOCK(lock) FoundationUtils::LockAndAutoFree FOUNDATION_LOCK_NAME(__LINE__)(&lock, __FILE__, __LINE__)
    class LockAndAutoFree {
    public:
        virtual ~LockAndAutoFree() {
            if (_Lock) {
                _Lock->Unlock();
            }
        }

        LockAndAutoFree(ILock* lock, const char* file, const int line) : _Lock(lock) {
            FoundationAssert(nullptr != lock, "lock nullptr");
            _Lock->Lock(file, line);
        }

        void Free() {
            if (_Lock) {
                _Lock->Unlock();
                _Lock = nullptr;
            }
        }

    private:
        ILock* _Lock;
    };

    class SpinLock : public ILock {
    public:
        virtual ~SpinLock() override {}
        SpinLock() : _LockThreadID(SafeSystem::Process::InvalidID), _Ref(0) {}

        bool TryLock() override {
            const UInt64 threadID = SafeSystem::Process::GetCurrentThreadID();
            if (_LockThreadID == threadID) {
                ++_Ref;
                return true;
            }
            if (_Flag.test_and_set(std::memory_order_acquire)) {
                return false;
            }
            _LockThreadID = threadID;
            _Ref = 1;
            return true;
        }

        void Lock(const char* file, const int line) override {
            if (_LockThreadID == SafeSystem::Process::InvalidID || _LockThreadID != SafeSystem::Process::GetCurrentThreadID()) {
                while (_Flag.test_and_set(std::memory_order_acquire)) {}
                _LockThreadID = SafeSystem::Process::GetCurrentThreadID();
            }
            _Ref++;
#ifdef _DEBUG
            _File = file;
            _Line = line;
#endif
            //printf("Lock on %s:%d\n", _File.c_str(), _Line);
        }

        void Unlock() override {
            FoundationAssert(_LockThreadID == SafeSystem::Process::GetCurrentThreadID(), "wtf");
            _Ref--;
            if (0 == _Ref) {
                _LockThreadID = SafeSystem::Process::InvalidID;
                _Flag.clear(std::memory_order_release);
            }
        }

        void ForceUnlock() override {
            _Ref = 0;
            _LockThreadID = SafeSystem::Process::InvalidID;
            _Flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag _Flag = ATOMIC_FLAG_INIT;
        UInt64 _LockThreadID;
        int _Ref;

#ifdef _DEBUG
        std::string _File;
        int _Line;
#endif //_DEBUG
    };
}

#endif // FOUNDATION_UTILS_X_LOCK_HPP



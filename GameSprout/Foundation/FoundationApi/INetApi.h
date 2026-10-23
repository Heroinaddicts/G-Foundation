#ifndef __iNetApi_h__
#define __iNetApi_h__

#include "MultiSys.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <mutex>

namespace Foundation {
    class Accepter;
    class Net;
    class SshTcpTunnel;
    class Tcper;
    class ITcpSession;

    namespace Detail {
        struct TcpSessionDispatchRegistry;
    }

    // Non-owning identity. The registry belongs to the module creating the Session,
    // so handles remain usable across the Foundation DLL boundary. No event owns a Session.
    struct TcpSessionDispatchHandle {
        Detail::TcpSessionDispatchRegistry* Registry = nullptr;
        std::uint64_t Id = 0;
        explicit operator bool() const { return Registry && Id; }
        bool operator==(const TcpSessionDispatchHandle&) const = default;
    };

    namespace Detail {
        struct TcpSessionDispatchState {
            std::recursive_mutex Mutex;
            ITcpSession* Session = nullptr;
            std::size_t References = 1;
            TcpSessionDispatchRegistry* Registry = nullptr;
        };

        struct TcpSessionDispatchRegistry {
            std::mutex Mutex;
            std::unordered_map<std::uint64_t, TcpSessionDispatchState*> Sessions;
            std::uint64_t NextId = 0;
        };
    }

    class ISocket {
    public:
        virtual ~ISocket() {}
    };

    class IAccepter {
    public:
        virtual ~IAccepter() {}
        virtual void Release() = 0;
    };

    class IPipe {
    public:
        virtual ~IPipe() {}

        virtual void Pause() = 0;
        virtual void Resume() = 0;
        virtual void Close() = 0;
    };

    enum class eTcpSessionEvent : int {
        Connected = 0,
        Disconnected = 1,
        Connectfaild = 2,

        __COUNT__
    };

    class ITcpPipe : public IPipe {
    public:
        virtual ~ITcpPipe() {}

        virtual void Send(const void* data, const int size, bool immediately) = 0;
        virtual std::string RemoteIp() const = 0;
        virtual int RemotePort() const = 0;
        virtual bool IsActiveConnect() const = 0;
        virtual void DetachSession(ITcpSession* session) = 0;
    };

    class ITcpSocket : public ISocket {
    private:
        ITcpPipe* _PipeValue;

    public:
        virtual ~ITcpSocket() {}
        ITcpSocket() : _PipeValue(nullptr), _Pipe(_PipeValue) {}

        ITcpPipe* const& _Pipe;

    private:
        friend class Accepter;
        friend class Net;
        friend class SshTcpTunnel;
        friend class Tcper;

        __forceinline void SetPipe(ITcpPipe* const pipe) {
            _PipeValue = pipe;
        }

    };

    class ITcpSession : public ITcpSocket {
    private:
        using TcpSessionDispatchState = Detail::TcpSessionDispatchState;

    public:
        // Owners must stop/join producers before destroying a derived Session on
        // another thread. Registry invalidation protects deferred dispatch only;
        // it does not synchronize arbitrary member access or derived destructors.
        virtual ~ITcpSession() {
            TcpSessionDispatchState* const state = _DispatchState;
            {
                std::lock_guard<std::mutex> guard(_DispatchLifetime.Registry->Mutex);
                _DispatchLifetime.Registry->Sessions.erase(_DispatchLifetime.Id);
            }
            {
                std::lock_guard<std::recursive_mutex> guard(state->Mutex);
                state->Session = nullptr;
            }
            ReleaseDispatchState(state);
            ITcpPipe* const pipe = _Pipe;
            if (pipe) {
                pipe->DetachSession(this);
            }
            for (int i = 0; i < (int)eTcpSessionEvent::__COUNT__; i++) {
                if (_TcpSessionEventPool[i]) {
                    txdel _TcpSessionEventPool[i];
                    _TcpSessionEventPool[i] = nullptr;
                }
            }
        }

        ITcpSession() {
            _DispatchState = new TcpSessionDispatchState();
            _DispatchState->Session = this;
            _DispatchState->Registry = &_DispatchRegistry;
            try {
                std::lock_guard<std::mutex> guard(_DispatchRegistry.Mutex);
                if (_DispatchRegistry.NextId == std::numeric_limits<std::uint64_t>::max()) {
                    throw std::overflow_error("TCP session dispatch IDs exhausted");
                }
                _DispatchLifetime = {&_DispatchRegistry, ++_DispatchRegistry.NextId};
                _DispatchRegistry.Sessions.emplace(_DispatchLifetime.Id, _DispatchState);
            }
            catch (...) {
                delete _DispatchState;
                throw;
            }
            for (int i = 0; i < (int)eTcpSessionEvent::__COUNT__; i++) {
                _TcpSessionEventPool[i] = nullptr;
            }
        }
        ITcpSession(const ITcpSession&) = delete;
        ITcpSession& operator=(const ITcpSession&) = delete;
        ITcpSession(ITcpSession&&) = delete;
        ITcpSession& operator=(ITcpSession&&) = delete;
        /*
        * 收到消息时回调(前提是没有Pause)
        * content 收到的数据地址
        * size 数据总长度
        * return int : 逻辑层处理了多少数据 就return多少, 解决逻辑层面拼包带来的内存操作开销, 逻辑层无需再拼包
        */
        virtual int OnReceive(const void* const content, const int size) = 0;

        void OnTcpSessionEvent(const eTcpSessionEvent ev) {
            DispatchTcpSessionEvent(_DispatchLifetime, ev);
        }

        TcpSessionDispatchHandle CaptureDispatchHandle() const {
            return _DispatchLifetime;
        }

        static bool DispatchTcpSessionEvent(
            TcpSessionDispatchHandle dispatchHandle,
            const eTcpSessionEvent ev
        ) {
            if (!dispatchHandle) {
                return false;
            }

            TcpSessionDispatchState* const state = AcquireDispatchState(dispatchHandle);
            if (!state) {
                return false;
            }
            bool dispatched = false;
            try {
                std::lock_guard<std::recursive_mutex> guard(state->Mutex);
                if (state->Session) {
                    state->Session->DispatchTcpSessionEventLocked(ev, state);
                    dispatched = true;
                }
            }
            catch (...) {
                ReleaseDispatchState(state);
                throw;
            }
            ReleaseDispatchState(state);
            return dispatched;
        }

        static bool DispatchReceive(
            TcpSessionDispatchHandle dispatchHandle,
            const void* const content,
            const int size,
            OUT int& consumed
        ) {
            consumed = 0;
            TcpSessionDispatchState* const state = AcquireDispatchState(dispatchHandle);
            if (!state) {
                return false;
            }
            bool dispatched = false;
            try {
                std::lock_guard<std::recursive_mutex> guard(state->Mutex);
                if (state->Session) {
                    consumed = state->Session->OnReceive(content, size);
                    dispatched = true;
                }
            }
            catch (...) {
                ReleaseDispatchState(state);
                throw;
            }
            ReleaseDispatchState(state);
            return dispatched;
        }

    private:
        void DispatchTcpSessionEventLocked(
            const eTcpSessionEvent ev,
            TcpSessionDispatchState* const state
        ) {
            const int eventIndex = static_cast<int>(ev);
            if (eventIndex < 0 || eventIndex >= (int)eTcpSessionEvent::__COUNT__) {
                return;
            }

            // 回调允许拥有者释放 Session。复制回调列表后，始终通过独立令牌
            // 判断对象是否仍然存在，绝不在析构后再次读取 Session 成员。
            std::vector<fTcpSessionEventCallback> callbacks;
            if (nullptr != _TcpSessionEventPool[eventIndex]) {
                callbacks = *_TcpSessionEventPool[eventIndex];
            }
            for (const fTcpSessionEventCallback& callback : callbacks) {
                callback(this);
                if (state->Session != this) {
                    return;
                }
            }

            // 虚事件处理函数放在最后调用；实现允许在其中释放自身，返回后
            // OnTcpSessionEvent 不再读取任何 Session 成员。
            switch (ev) {
            case eTcpSessionEvent::Connectfaild: {
                OnConnectFailed();
                break;
            }
            case eTcpSessionEvent::Connected: {
                OnConnected();
                break;
            }
            case eTcpSessionEvent::Disconnected: {
                OnDisconnected();
                break;
            }
            }
        }

    public:

        __forceinline void Close() const { if (this && _Pipe) { _Pipe->Close(); } }
        __forceinline void Send(const void* context, const int size, bool immediately = true) const { if (this && _Pipe) { _Pipe->Send(context, size, immediately); } }
        __forceinline void Pause() { if (this && _Pipe) { _Pipe->Pause(); } }
        __forceinline void Resume() { if (this && _Pipe) { _Pipe->Resume(); } }

        __forceinline std::string RemoteIp() const {
            if (this && _Pipe) {
                return _Pipe->RemoteIp();
            }

            return "";
        }

        __forceinline int RemotePort() const {
            if (this && _Pipe) {
                return _Pipe->RemotePort();
            }

            return -1;
        }

        __forceinline bool IsActiveConnect() const {
            if (this && _Pipe) {
                return _Pipe->IsActiveConnect();
            }

            return false;
        }

        template<typename T>
        void RegistTcpSessionEventCallback(const eTcpSessionEvent ev, std::function<void(T* const)> fun) {
            PrivateRegisterTcpSessionEventCallback(ev, [=](ITcpSession* const session) {
                fun((T* const)session);
                }
            );
        }

    protected:
        virtual void OnConnected() {}
        virtual void OnConnectFailed() {}
        virtual void OnDisconnected() {}

    private:
        typedef std::function<void(ITcpSession* const)> fTcpSessionEventCallback;

        virtual void PrivateRegisterTcpSessionEventCallback(const eTcpSessionEvent ev, const fTcpSessionEventCallback& fun) {
            const int eventIndex = static_cast<int>(ev);
            if (eventIndex < 0 || eventIndex >= (int)eTcpSessionEvent::__COUNT__) {
                return;
            }
            if (nullptr == _TcpSessionEventPool[eventIndex]) {
                _TcpSessionEventPool[eventIndex] = txnew std::vector<fTcpSessionEventCallback>();
            }
            _TcpSessionEventPool[eventIndex]->push_back(fun);
        }

        std::vector<fTcpSessionEventCallback>* _TcpSessionEventPool[(int)eTcpSessionEvent::__COUNT__];
        TcpSessionDispatchHandle _DispatchLifetime{};
        TcpSessionDispatchState* _DispatchState = nullptr;

        static TcpSessionDispatchState* AcquireDispatchState(TcpSessionDispatchHandle id) {
            if (!id) {
                return nullptr;
            }
            std::lock_guard<std::mutex> guard(id.Registry->Mutex);
            const auto iterator = id.Registry->Sessions.find(id.Id);
            if (iterator == id.Registry->Sessions.end()) {
                return nullptr;
            }
            ++iterator->second->References;
            return iterator->second;
        }

        static void ReleaseDispatchState(TcpSessionDispatchState* state) {
            bool destroy;
            {
                std::lock_guard<std::mutex> guard(state->Registry->Mutex);
                destroy = --state->References == 0;
            }
            if (destroy) {
                delete state;
            }
        }

        // References count the Session owner plus currently executing dispatches,
        // never queued events. All increments/decrements occur under this mutex.
        // The registry mutex is never held while invoking a callback.
        inline static Detail::TcpSessionDispatchRegistry _DispatchRegistry;
    };

    class ITcpServer {
    private:
        IAccepter* _AccepterValue;

    public:
        ITcpServer() : _AccepterValue(nullptr), _Accepter(_AccepterValue) {}
        virtual ~ITcpServer() {}

        virtual ITcpSession* OnMallocTcpSession(const std::string& remoteIp, const UInt16 remotePort) = 0;
        virtual void OnError(ITcpSession* session) = 0;
        virtual void OnRelease() = 0;

        virtual void Release() { if (_Accepter) { _Accepter->Release(); } }

        IAccepter* const& _Accepter;

    private:
        friend class Accepter;

        __forceinline void SetAccepter(IAccepter* const accepter) {
            _AccepterValue = accepter;
        }

    };

    class INetApi {
    public:
        virtual ~INetApi() {}

        virtual bool LaunchTcpSession(ITcpSession* session, const std::string& host, const UInt16 port, const int size) = 0;
        virtual bool LaunchTcpServer(ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size) = 0;
    };
}

#endif //__iNetApi_h__

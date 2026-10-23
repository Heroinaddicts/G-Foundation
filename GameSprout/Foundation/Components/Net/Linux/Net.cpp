#include "Net.h"
#include "Tcper.h"
#include "Accepter.h"

#include <unordered_set>
#include <vector>

namespace Foundation {
    bool Net::Initialize(FoundationApi* const foundation) {
        if (!foundation || _Foundation || _EpollerFd != INVALID_FD) {
            return false;
        }
        if (INVALID_FD == (_EpollerFd = epoll_create(EPOLLER_DESC_COUNT))) {
            FError("create epoller fd error %s", strerror(errno));
            return false;
        }

        _Foundation = foundation;
        if (!Start(1)) {
            close(_EpollerFd);
            _EpollerFd = INVALID_FD;
            _Foundation = nullptr;
            return false;
        }
        return true;
    }

    bool Net::Launch(FoundationApi* const foundation) {
        return foundation == _Foundation;
    }

    void Net::Release(FoundationApi* const foundation) {
        if (foundation != _Foundation) {
            return;
        }
        RequestStop();
        Wait();

        NetRequest* request = nullptr;
        while (_RequestQueue.Pull(OUT request)) {
            txdel request;
        }

        NetEvent* event = nullptr;
        while (_EventQueue.Pull(OUT event)) {
            txdel event;
        }

        std::vector<Accepter*> acceptors;
        std::vector<Tcper*> tcpers;
        {
            AUTO_LOCK(_ObjectsLock);
            acceptors.assign(_Acceptors.begin(), _Acceptors.end());
            tcpers.assign(_Tcpers.begin(), _Tcpers.end());
        }
        for (Accepter* accepter : acceptors) {
            if (accepter) {
                accepter->Release();
            }
        }
        for (Tcper* tcper : tcpers) {
            if (tcper) {
                Tcper::Release(tcper);
            }
        }
        {
            AUTO_LOCK(_ObjectsLock);
            _Acceptors.clear();
            _Tcpers.clear();
        }

        if (_EpollerFd != INVALID_FD) {
            close(_EpollerFd);
            _EpollerFd = INVALID_FD;
        }
        _Foundation = nullptr;
    }

    void Net::EarlyUpdate(FoundationApi* const foundation) {
        NetEvent* event = nullptr;
        std::unordered_set<Tcper*> tcpersToRelease;
        const Int64 tick = SafeSystem::Time::GetMicroSecond();
        while (_EventQueue.Pull(OUT event)) {
            switch (event->_Type) {
            case eNetEventType::Accept: {
                ITcpSession* session = event->_Accepter->_Server->OnMallocTcpSession(event->_IP, event->_Port);
                if (session) {
                    event->_Tcper->CorrelateTcpSession(session);
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
                    event->_Tcper->ProcessData(session->CaptureDispatchHandle());
                }
                else {
                    tcpersToRelease.insert(event->_Tcper);
                }
                break;
            }
            case eNetEventType::Recv: {
                const TcpSessionDispatchHandle currentDispatch =
                    event->_Tcper->GetTcpSessionDispatch();
                if (event->_SessionDispatch
                    && currentDispatch == event->_SessionDispatch) {
                    event->_Tcper->ProcessData(event->_SessionDispatch);
                }
                break;
            }
            case eNetEventType::Connect: {
                const TcpSessionDispatchHandle currentDispatch =
                    event->_Tcper->GetTcpSessionDispatch();
                if (!event->_SessionDispatch
                    || currentDispatch != event->_SessionDispatch) {
                    break;
                }
                if (event->_Code == 0) {
                    ITcpSession::DispatchTcpSessionEvent(
                        event->_SessionDispatch,
                        eTcpSessionEvent::Connected);
                }
                else {
                    XLOG(foundation, "Connect error %d", event->_Code);
                    event->_Tcper->CorrelateTcpSession(nullptr);
                    ITcpSession::DispatchTcpSessionEvent(
                        event->_SessionDispatch,
                        eTcpSessionEvent::Connectfaild);
                    tcpersToRelease.insert(event->_Tcper);
                }
                break;
            }
            case eNetEventType::Disconnect: {
                const TcpSessionDispatchHandle currentDispatch =
                    event->_Tcper->GetTcpSessionDispatch();
                if (event->_SessionDispatch
                    && currentDispatch == event->_SessionDispatch) {
                    event->_Tcper->CorrelateTcpSession(nullptr);
                    ITcpSession::DispatchTcpSessionEvent(
                        event->_SessionDispatch,
                        eTcpSessionEvent::Disconnected);
                }
                tcpersToRelease.insert(event->_Tcper);
                break;
            }
            default:
                break;
            }
            txdel event;

            if (SafeSystem::Time::GetMicroSecond() - tick > 5000) {
                break;
            }
        }

        for (Tcper* tcper : tcpersToRelease) {
            NetRequest* request = txnew NetRequest(eNetRequestType::ReleaseTcper);
            request->_Tcper = tcper;
            _RequestQueue.Push(request);
        }
    }

    void Net::Update(FoundationApi* const foundation) {
    }

    void Net::LaterUpdate(FoundationApi* const foundation) {
    }

    bool Net::LaunchTcpSession(ITcpSession* session, const std::string& host, const UInt16 port, const int size) {
        return nullptr != Tcper::Create(this, -1, host, port, true, session, size);
    }

    bool Net::LaunchTcpServer(ITcpServer* server, const std::string& listenIp, const UInt16 listenPort, const int size) {
        return nullptr != Accepter::Create(this, server, listenIp, listenPort, size);
    }

    int Net::AddEpoll(int fd, UInt32 events, void* userData) {
        if (_EpollerFd == INVALID_FD) {
            return ERROR_SOCKET;
        }
        epoll_event event{};
        event.data.ptr = userData;
        event.events = events;
        return epoll_ctl(_EpollerFd, EPOLL_CTL_ADD, fd, &event);
    }

    void Net::DeleteEpoll(int fd) {
        if (_EpollerFd != INVALID_FD && fd != INVALID_FD) {
            epoll_ctl(_EpollerFd, EPOLL_CTL_DEL, fd, nullptr);
        }
    }

    void Net::PushEvent(NetEvent* event) {
        if (event) {
            _EventQueue.Push(event);
        }
    }

    void Net::PushRequest(NetRequest* request) {
        if (request) {
            _RequestQueue.Push(request);
        }
    }

    void Net::RegisterTcper(Tcper* tcper) {
        if (tcper) {
            AUTO_LOCK(_ObjectsLock);
            _Tcpers.insert(tcper);
        }
    }

    void Net::UnregisterTcper(Tcper* tcper) {
        AUTO_LOCK(_ObjectsLock);
        _Tcpers.erase(tcper);
    }

    void Net::RegisterAccepter(Accepter* accepter) {
        if (accepter) {
            AUTO_LOCK(_ObjectsLock);
            _Acceptors.insert(accepter);
        }
    }

    void Net::UnregisterAccepter(Accepter* accepter) {
        AUTO_LOCK(_ObjectsLock);
        _Acceptors.erase(accepter);
    }

    void Net::Run(const int threadIndex, void* const context) {
        epoll_event events[EPOLLER_MAX_EVENT_COUNT];
        while (!IsStopRequested()) {
            const Int64 tick = SafeSystem::Time::GetMicroSecond();
            errno = 0;
            const int count = epoll_wait(
                _EpollerFd,
                events,
                EPOLLER_MAX_EVENT_COUNT,
                1);
            for (int i = 0; i < count; ++i) {
                Associat* association = static_cast<Associat*>(events[i].data.ptr);
                if (association) {
                    association->_Completer->OnCompleter(
                        association,
                        association->_Ev,
                        events[i]);
                }
            }

            NetRequest* request = nullptr;
            while (_RequestQueue.Pull(OUT request)) {
                switch (request->_Type) {
                case eNetRequestType::ReleaseTcper:
                    Tcper::Release(request->_Tcper);
                    break;
                case eNetRequestType::CloseTcper: {
                    NetEvent* event = txnew NetEvent(eNetEventType::Disconnect);
                    event->_Tcper = request->_Tcper;
                    event->_SessionDispatch = request->_Tcper->GetTcpSessionDispatch();
                    _EventQueue.Push(event);
                    break;
                }
                default:
                    break;
                }
                txdel request;
            }

            if (SafeSystem::Time::GetMicroSecond() - tick <= 100) {
                SafeSystem::Time::MillisecondSleep(1);
            }
        }
    }
}

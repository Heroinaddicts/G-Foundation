
#include "Net.h"
#include "Tcper.h"
#include "Accepter.h"
#include "SafeSystem.hpp"
#include <unordered_set>
#include <vector>

namespace Foundation {
    bool Net::Initialize(FoundationApi* const foundation) {
        if (!foundation || _Foundation || _KqueueFd != INVALID_FD) {
            return false;
        }
        if (INVALID_FD == (_KqueueFd = kqueue())) {
            FError("create kqueue fd error %s", strerror(errno));
            return false;
        }

        _Foundation = foundation;
        if (!Start(1)) {
            close(_KqueueFd);
            _KqueueFd = INVALID_FD;
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

        NetRequest* req = nullptr;
        while (_RequestQueue.Pull(OUT req)) {
            txdel req;
        }

        NetEvent* ev = nullptr;
        while (_EventQueue.Pull(OUT ev)) {
            txdel ev;
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

        if (INVALID_FD != _KqueueFd) {
            close(_KqueueFd);
            _KqueueFd = INVALID_FD;
        }
        _Foundation = nullptr;
    }

    void Net::EarlyUpdate(FoundationApi* const foundation) {
        NetEvent* netev = nullptr;
        std::unordered_set<Tcper*> tcpersToRelease;

        while (_EventQueue.Pull(OUT netev)) {
            switch (netev->_Type) {
            case eNetEventType::Accept: {
                ITcpSession* session = netev->_Accepter->_Server->OnMallocTcpSession(netev->_IP, netev->_Port);
                if (nullptr != session) {
                    netev->_Tcper->CorrelateTcpSession(session);
                    session->OnTcpSessionEvent(eTcpSessionEvent::Connected);
                    // The peer may send its first frame before the Accept event
                    // reaches the owner thread. Such bytes are already in the
                    // transport buffer, but their earlier Recv notification had
                    // no Session dispatch handle. Drain them immediately after
                    // binding so protocol registration/login cannot stall.
                    netev->_Tcper->ProcessData(session->CaptureDispatchHandle());
                }
                else {
                    tcpersToRelease.insert(netev->_Tcper);
                }
                break;
            }
            case eNetEventType::Recv: {
                const TcpSessionDispatchHandle currentDispatch =
                    netev->_Tcper->GetTcpSessionDispatch();
                if (netev->_SessionDispatch
                    && currentDispatch == netev->_SessionDispatch) {
                    netev->_Tcper->ProcessData(netev->_SessionDispatch);
                }
                break;
            }
            case eNetEventType::Connect: {
                const TcpSessionDispatchHandle currentDispatch =
                    netev->_Tcper->GetTcpSessionDispatch();
                if (!netev->_SessionDispatch
                    || currentDispatch != netev->_SessionDispatch) {
                    // The transport was detached after this event was queued.
                    // Its close path owns transport release.  Do not enqueue a
                    // second release here: pooled Tcper storage may already
                    // have been recycled for a newer connection.
                    break;
                }
                if (netev->_Code == 0) {
                    ITcpSession::DispatchTcpSessionEvent(
                        netev->_SessionDispatch,
                        eTcpSessionEvent::Connected
                    );
                }
                else {
                    XLOG(foundation, "Connect error %d", netev->_Code);
                    netev->_Tcper->CorrelateTcpSession(nullptr);
                    ITcpSession::DispatchTcpSessionEvent(
                        netev->_SessionDispatch,
                        eTcpSessionEvent::Connectfaild
                    );
                    tcpersToRelease.insert(netev->_Tcper);
                }
                break;
            }
            case eNetEventType::Disconnect: {
                const TcpSessionDispatchHandle currentDispatch =
                    netev->_Tcper->GetTcpSessionDispatch();
                if (netev->_SessionDispatch
                    && currentDispatch == netev->_SessionDispatch) {
                    netev->_Tcper->CorrelateTcpSession(nullptr);
                    ITcpSession::DispatchTcpSessionEvent(
                        netev->_SessionDispatch,
                        eTcpSessionEvent::Disconnected
                    );
                }

                tcpersToRelease.insert(netev->_Tcper);
                break;
            }
            default:
                break;
            }
            txdel netev;
        }

        // A closed socket can yield more than one queued notification. Drain
        // the event queue first, then release each transport exactly once so
        // no later event in this update can reference recycled pool storage.
        for (Tcper* const tcper : tcpersToRelease) {
            NetRequest* req = txnew NetRequest(eNetRequestType::ReleaseTcper);
            req->_Tcper = tcper;
            _RequestQueue.Push(req);
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

    int Net::AddKqueueEvent(int fd, int16_t filter, void* userData) {
        if (_KqueueFd == INVALID_FD) {
            return ERROR_SOCKET;
        }
        struct kevent event;
        EV_SET(&event, fd, filter, EV_ADD, 0, 0, userData);
        return kevent(_KqueueFd, &event, 1, nullptr, 0, nullptr);
    }

    int Net::DeleteKqueueEvent(int fd, int16_t filter) {
        if (_KqueueFd == INVALID_FD) {
            return ERROR_SOCKET;
        }
        struct kevent event;
        EV_SET(&event, fd, filter, EV_DELETE, 0, 0, nullptr);
        return kevent(_KqueueFd, &event, 1, nullptr, 0, nullptr);
    }

    int Net::AddKqueueRead(int fd, void* userData) {
        return AddKqueueEvent(fd, EVFILT_READ, userData);
    }

    int Net::AddKqueueWrite(int fd, void* userData) {
        return AddKqueueEvent(fd, EVFILT_WRITE, userData);
    }

    void Net::DeleteKqueueReadWrite(int fd) {
        DeleteKqueueEvent(fd, EVFILT_READ);
        DeleteKqueueEvent(fd, EVFILT_WRITE);
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
        struct kevent evs[KQUEUE_MAX_EVENT_COUNT];
        const struct timespec timeout = {0, 1000000};
        while (!IsStopRequested()) {
            errno = 0;
            int count = 0;
            if ((count = kevent(
                    _KqueueFd,
                    nullptr,
                    0,
                    evs,
                    KQUEUE_MAX_EVENT_COUNT,
                    &timeout
                )) > 0) {
                for (int i = 0; i < count; i++) {
                    struct Associat* p = (struct Associat*)evs[i].udata;
                    if (nullptr == p) {
                        continue;
                    }
                    p->_Completer->OnCompleter(p, p->_Ev, evs[i]);
                }
            }

            NetRequest* req = nullptr;
            while (_RequestQueue.Pull(OUT req)) {
                switch (req->_Type) {
                case eNetRequestType::ReleaseTcper: {
                    Tcper::Release(req->_Tcper);
                    break;
                }
                case eNetRequestType::CloseTcper: {
                    NetEvent* ev = txnew NetEvent(eNetEventType::Disconnect);
                    ev->_Tcper = req->_Tcper;
                    ev->_SessionDispatch = req->_Tcper->GetTcpSessionDispatch();
                    _EventQueue.Push(ev);
                    break;
                }
                default:
                    break;
                }

                txdel req;
            }
        }
    }
}

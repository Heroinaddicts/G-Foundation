#include "Net.h"
#include "Tcper.h"
#include "Accepter.h"

#include "Foundation.h"
#include "SafeSystem.hpp"

namespace Foundation {
    FoundationUtils::XPool<OVERLAPPEDEX> g_OverlappedexPool;
    HANDLE g_CompletePort;

    INet* Net::GetInstance() {
        static Net s_Instance;
        return &s_Instance;
    }

    bool Net::Initialize(FoundationApi* const foundation) {
        WSADATA wsa;
        int err = WSAStartup(MAKEWORD(1, 1), &wsa);
        if (err != 0) {
            FError("WSAStartup error");
            return false;
        }

        if (LOBYTE(wsa.wVersion) != 1 || HIBYTE(wsa.wVersion) != 1) {
            FError("WSAStartup LOBYTE or HIBYTE error");
            WSACleanup();
            return false;
        }
        g_CompletePort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (nullptr == g_CompletePort) {
            FError("CreateIoCompletionPort error : %s", strerror(::GetLastError()));
            return false;
        }

        return true;
    }

    bool Net::Launch(FoundationApi* const foundation) {
        return true;
    }

    void Net::Release(FoundationApi* const foundation) {
        ReleaseRetiredTcpPipes();
    }

    void Net::EarlyUpdate(FoundationApi* const foundation) {
        Int64 tick = FoundationUtils::SafeSystem::Time::GetMicroSecond();
        do {
            DWORD bytes = 0;
            SOCKET sock = INVALID_SOCKET;
            OVERLAPPEDEX* plus = nullptr;
            SetLastError(0);

            int err = 0;
            BOOL res = GetQueuedCompletionStatus(g_CompletePort, &bytes, (PULONG_PTR)&sock, (LPOVERLAPPED*)&plus, 1);
            if (!res) {
                err = GetLastError();
                if (WAIT_TIMEOUT == err) {
                    break;
                }
            }

            if (nullptr == plus) {
                continue;
            }

            plus->_Completer->OnCompleted(plus, plus->_Type, err, bytes);
        } while (FoundationUtils::SafeSystem::Time::GetMicroSecond() - tick < 2000);
        ReleaseRetiredTcpPipes();
    }

    void Net::Update(FoundationApi* const foundation) {

    }

    void Net::LaterUpdate(FoundationApi* const foundation) {

    }

    void Net::RetireTcpPipe(Tcper* const pipe) {
        if (pipe) {
            _RetiredTcpPipes.push_back(pipe);
        }
    }

    void Net::ReleaseRetiredTcpPipes() {
        std::vector<Tcper*> retiredTcpPipes;
        retiredTcpPipes.swap(_RetiredTcpPipes);
        for (Tcper* const pipe : retiredTcpPipes) {
            XPOOL_RELEASE(s_TcperPool, pipe);
        }
    }

    bool Net::LaunchTcpSession(ITcpSession* session, const std::string& host, const UInt16 port, const int size) {
        SetLastError(0);
        DWORD ul = 1;
        int len = 0;
        LINGER linger = { 1,0 };

        bool initiative = true;

        std::string ip;
        if (!GetIpByHost(host.c_str(), ip)) {
            session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
            return false;
        }

        int socket = INVALID_SOCKET;
        sockaddr_in addr;
        FoundationUtils::SafeMemory::Memset(&addr, sizeof(addr), 0, sizeof(addr));
        addr.sin_family = AF_INET;
        if (INVALID_SOCKET == (socket = (int)WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED))
            || SOCKET_ERROR == setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&len, sizeof(int))
            || SOCKET_ERROR == setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char*)&len, sizeof(int))
            || setsockopt(socket, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger))
            || SOCKET_ERROR == ioctlsocket(socket, FIONBIO, &ul)
            || (SOCKET_ERROR == ::bind(socket, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)))
            || CreateIoCompletionPort((HANDLE)socket, (HANDLE)g_CompletePort, (u_long)socket, 0) != g_CompletePort) {
            SAFE_CLOSE_SOCKET(socket);
            session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
            return false;
        }

        Tcper* pipe = Tcper::Create(session, ip, port, socket, true, size);
        if (nullptr == pipe) {
            SAFE_CLOSE_SOCKET(socket);
            session->OnTcpSessionEvent(eTcpSessionEvent::Connectfaild);
            return false;
        }

        session->SetPipe(pipe);
        return true;
    }

    bool Net::LaunchTcpServer(ITcpServer* server, const std::string& ip, const UInt16 port, const int size) {
        Accepter* ac = Accepter::Create(server, ip, port, size);
        return ac != nullptr;
    }
}

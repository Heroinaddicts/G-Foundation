#pragma once

#include "IFoundation.h"
#include "INet.h"
#include "Foundation.h"

#include "SafeMemory.hpp"
#include "SafeSystem.hpp"
#include "SafeThread.hpp"
#include "SafeQueue.hpp"
#include "XPool.hpp"
#include "XBuffer.hpp"
#include "XLock.hpp"

#include <queue>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/resource.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netdb.h>
#include <set>

#define ERROR_SOCKET -1
#define INVALID_FD -1
#define SUCCESS 0
#define EPOLLER_MAX_EVENT_COUNT 512
#define EPOLLER_DESC_COUNT 10240
#define MAX_PIPE_SIZE (20 * 1024 * 1024)
#define RECV_TEMP_SIZE 512

using namespace FoundationUtils;

namespace Foundation
{
    enum eCompletion
    {
        doAccept,
        doConnect,
        doIO,
    };

    class iCompleter;
    struct Associat
    {
        const eCompletion _Ev;
        iCompleter* const _Completer;

        Associat(const eCompletion ev, iCompleter* const p) : _Ev(ev), _Completer(p) {}
    };

    class iCompleter
    {
    public:
        virtual ~iCompleter() {}

        virtual void OnCompleter(Associat* at, const eCompletion type, const struct epoll_event& ev) = 0;
    };

    class Tcper;
    class Accepter;

#define CLOSE_SOCKET(sock) \
    { \
        ::shutdown(sock, SHUT_RDWR); \
        ::close(sock);     \
        sock = INVALID_FD; \
    }

    enum eNetEventType
    {
        Accept,
        Connect,
        Disconnect,
        Recv
    };

    struct NetEvent
    {
        const eNetEventType _Type;
        int _Code;
        Int64 _Sock;
        std::string _IP;
        int _Port;
        Accepter* _Accepter;
        Tcper* _Tcper;
        TcpSessionDispatchHandle _SessionDispatch{};

        NetEvent(const eNetEventType type) : _Type(type), _Code(0), _Accepter(nullptr), _Tcper(nullptr) {}
    };
    enum eNetRequestType
    {
        ReleaseTcper,
        CloseTcper
    };

    struct NetRequest
    {
        const eNetRequestType _Type;
        Tcper* _Tcper;

        NetRequest(const eNetRequestType type) : _Type(type) {}
    };
#define CONNECTED_COUNT    // ConnectedCount(__FILE__, __LINE__)
#define DISCONNECTED_COUNT // DisconnectedCount(__FILE__, __LINE__)
}

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus__

    __forceinline int SetNonBlocking(int sockfd)
    {
        return fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);
    }

    __forceinline int SetTcpNoDelay(const int fd)
    {
        int val = 1;
        return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val));
    }

    __forceinline int SetTcpQuickAck(const int fd)
    {
        int val = 1;
        return setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, (const char*)&val, sizeof(val));
    }

    __forceinline int SetReuse(const int fd)
    {
        int val = 1;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&val, sizeof(val));
    }

    __forceinline int SetSockSendBuff(int sock, int size)
    {
        return setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&size, sizeof(size));
    }

    __forceinline int SetSockRecvBuff(int sock, int size)
    {
        return setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&size, sizeof(size));
    }

    __forceinline bool SetMaxOpenFile(const int size)
    {
        struct rlimit rt;
        rt.rlim_max = rt.rlim_cur = size;
        return setrlimit(RLIMIT_NOFILE, &rt) != -1;
    }

    __forceinline bool SetStackSize(const int size)
    {
        struct rlimit rt;
        rt.rlim_max = rt.rlim_cur = size * 1024;
        return setrlimit(RLIMIT_STACK, &rt) != -1;
    }

    __forceinline bool GetIpByHost(const char* host, OUT std::string& ip)
    {
        struct hostent* hp;
        if ((hp = gethostbyname(host)) == nullptr)
        {
            return false;
        }

        ip = inet_ntoa(*(struct in_addr*)hp->h_addr);
        return true;
    }

#ifdef __cplusplus
}
#endif //__cplusplus

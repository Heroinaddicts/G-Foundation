#ifndef HTTP_H
#define HTTP_H

#include "Header.h"

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Foundation {
    class Http final : public IHttp, private FoundationUtils::SafeThread {
    public:
        ~Http() override {}

        bool Initialize(FoundationApi* foundation) override;
        bool Launch(FoundationApi* foundation) override;
        void Release(FoundationApi* foundation) override;
        void EarlyUpdate(FoundationApi* foundation) override;
        void Update(FoundationApi* foundation) override;
        void LaterUpdate(FoundationApi* foundation) override;

        HttpSubmitResult Submit(const HttpRequest& request, const HttpCallbacks& callbacks) override;
        bool Cancel(UInt64 requestId) override;
        void PostCallback(std::function<void()> callback);

    private:
        friend class Foundation;
        Http() {}

        void Run(int threadIndex, void* context) override;
        void WakeWorker();

        std::mutex _RequestMutex;
        std::deque<HttpRequestState*> _PendingRequests;
        std::unordered_map<UInt64, HttpRequestState*> _Requests;
        std::mutex _CallbackMutex;
        std::deque<std::function<void()>> _Callbacks;
        std::atomic<void*> _MultiHandle = nullptr;
        UInt64 _NextRequestId = 1;
        bool _AcceptingRequests = false;
        FoundationApi* _Foundation = nullptr;
    };
}

#endif // HTTP_H

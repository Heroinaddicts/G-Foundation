#ifndef HTTP_HEADER_H
#define HTTP_HEADER_H

#include "IHttp.h"
#include "SafeThread.hpp"

#include <atomic>

namespace Foundation {
    struct HttpRequestState final {
        HttpRequestState(const UInt64 requestId, const HttpRequest& request, const HttpCallbacks& callbacks)
            : _RequestId(requestId)
            , _Request(request)
            , _Callbacks(callbacks) {
        }

        UInt64 _RequestId = 0;
        HttpRequest _Request;
        HttpCallbacks _Callbacks;
        std::atomic_bool _CancellationRequested = false;
    };
}

#endif // HTTP_HEADER_H

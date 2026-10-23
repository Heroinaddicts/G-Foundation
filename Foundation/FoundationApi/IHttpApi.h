#ifndef HTTP_API_H
#define HTTP_API_H

#include "MultiSys.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Foundation {
    inline constexpr std::size_t HttpDefaultMaximumResponseSize = 16 * 1024 * 1024;

    enum class eHttpMethod : int {
        Get,
        Post,
        Put,
        Patch,
        Delete,
        Head
    };

    enum class eHttpErrorCode : int {
        None,
        InvalidRequest,
        SystemUnavailable,
        Cancelled,
        Timeout,
        TransportFailure,
        ResponseTooLarge
    };

    struct HttpError final {
        eHttpErrorCode _Code = eHttpErrorCode::None;
        Int64 _PlatformCode = 0;
        std::string _Message;

        bool Succeeded() const {
            return _Code == eHttpErrorCode::None;
        }
    };

    struct HttpHeader final {
        std::string _Name;
        std::string _Value;
    };

    struct HttpRequest final {
        std::string _Url;
        eHttpMethod _Method = eHttpMethod::Get;
        std::vector<HttpHeader> _Headers;
        std::vector<UInt8> _Body;
        UInt64 _ConnectTimeoutMilliseconds = 10000;
        UInt64 _RequestTimeoutMilliseconds = 120000;
        // Optional response inactivity guard. When non-zero, libcurl aborts
        // only after the transfer remains below one byte per second for this
        // many seconds. This is independent from the absolute request
        // timeout and is suitable for long-lived streaming responses.
        UInt64 _InactivityTimeoutSeconds = 0;
        std::size_t _MaximumResponseSize = HttpDefaultMaximumResponseSize;
        bool _FollowRedirects = true;
    };

    struct HttpResponse final {
        UInt64 _RequestId = 0;
        Int64 _StatusCode = 0;
        std::vector<HttpHeader> _Headers;
        std::vector<UInt8> _Body;
        std::string _FinalUrl;
        HttpError _Error;

        bool TransportSucceeded() const {
            return _Error.Succeeded();
        }

        bool Succeeded() const {
            return TransportSucceeded() && _StatusCode >= 200 && _StatusCode < 300;
        }
    };

    struct HttpCallbacks final {
        // Both callbacks are dispatched by FoundationApi::Update().
        std::function<void(UInt64, const std::vector<UInt8>&)> _DataReceived;
        std::function<void(const HttpResponse&)> _Completed;
    };

    enum class eHttpSubmitStatus : int {
        Accepted,
        InvalidRequest,
        SystemUnavailable
    };

    struct HttpSubmitResult final {
        eHttpSubmitStatus _Status = eHttpSubmitStatus::InvalidRequest;
        UInt64 _RequestId = 0;

        bool Accepted() const {
            return _Status == eHttpSubmitStatus::Accepted && _RequestId != 0;
        }
    };

    class IHttpApi {
    public:
        virtual ~IHttpApi() {}

        virtual HttpSubmitResult Submit(const HttpRequest& request, const HttpCallbacks& callbacks) = 0;
        virtual bool Cancel(UInt64 requestId) = 0;
    };
}

#endif // HTTP_API_H

#include "Http.h"

#include <curl/curl.h>

#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Foundation {
    namespace {
        struct CurlRequestContext final {
            Http* _Owner = nullptr;
            HttpRequestState* _State = nullptr;
            CURL* _EasyHandle = nullptr;
            curl_slist* _RequestHeaders = nullptr;
            HttpResponse _Response;
            std::size_t _ReceivedSize = 0;
            Int64 _ForcedPlatformCode = 0;
            std::string _ForcedErrorMessage;
            char _ErrorBuffer[CURL_ERROR_SIZE]{};
            bool _ResponseTooLarge = false;
            bool _AddedToMulti = false;
        };

        std::string_view Trim(std::string_view value) {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty()
                && (value.back() == ' ' || value.back() == '\t'
                    || value.back() == '\r' || value.back() == '\n')) {
                value.remove_suffix(1);
            }
            return value;
        }

        std::size_t OnBody(char* const data, const std::size_t size, const std::size_t count, void* const contextPointer) {
            CurlRequestContext* const context = static_cast<CurlRequestContext*>(contextPointer);
            const std::size_t byteCount = size * count;
            if (!context || !context->_State || !data || byteCount == 0) {
                return byteCount;
            }
            const std::size_t maximumSize = context->_State->_Request._MaximumResponseSize;
            if (byteCount > maximumSize
                || context->_ReceivedSize > maximumSize - byteCount) {
                context->_ResponseTooLarge = true;
                return 0;
            }
            context->_ReceivedSize += byteCount;

            std::vector<UInt8> chunk(byteCount);
            std::memcpy(chunk.data(), data, byteCount);
            try {
                context->_Response._Body.insert(context->_Response._Body.end(), chunk.begin(), chunk.end());
            }
            catch (...) {
                context->_ResponseTooLarge = true;
                return 0;
            }

            const auto callback = context->_State->_Callbacks._DataReceived;
            if (callback && context->_Owner) {
                const UInt64 requestId = context->_State->_RequestId;
                context->_Owner->PostCallback([callback, requestId, chunk = std::move(chunk)]() {
                    callback(requestId, chunk);
                });
            }
            return byteCount;
        }

        std::size_t OnHeader(char* const data, const std::size_t size, const std::size_t count, void* const contextPointer) {
            CurlRequestContext* const context = static_cast<CurlRequestContext*>(contextPointer);
            const std::size_t byteCount = size * count;
            if (!context || !data || byteCount == 0) {
                return byteCount;
            }
            const std::string_view line(data, byteCount);
            const std::size_t separator = line.find(':');
            if (separator == std::string_view::npos) {
                return byteCount;
            }
            const std::string_view name = Trim(line.substr(0, separator));
            const std::string_view value = Trim(line.substr(separator + 1));
            if (!name.empty()) {
                context->_Response._Headers.push_back({
                    std::string(name),
                    std::string(value)
                });
            }
            return byteCount;
        }

        int OnProgress(void* const contextPointer, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            const CurlRequestContext* const context = static_cast<CurlRequestContext*>(contextPointer);
            return context
                && context->_State
                && context->_State->_CancellationRequested.load(std::memory_order_acquire)
                ? 1
                : 0;
        }
    }

    bool Http::Initialize(FoundationApi* const foundation) {
        if (!foundation || _Foundation || RunningThreadCount() != 0) {
            return false;
        }
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            return false;
        }
        _Foundation = foundation;
        _NextRequestId = 1;
        _AcceptingRequests = false;
        return true;
    }

    bool Http::Launch(FoundationApi* const foundation) {
        if (foundation != _Foundation
            || _AcceptingRequests
            || RunningThreadCount() != 0
            || _MultiHandle.load(std::memory_order_acquire)) {
            return false;
        }
        CURLM* const multiHandle = curl_multi_init();
        if (!multiHandle) {
            return false;
        }
        constexpr long MaximumTotalConnectionCount = 256;
        constexpr long MaximumHostConnectionCount = 64;
        if (curl_multi_setopt(
                multiHandle,
                CURLMOPT_MAX_TOTAL_CONNECTIONS,
                MaximumTotalConnectionCount
            ) != CURLM_OK
            || curl_multi_setopt(
                multiHandle,
                CURLMOPT_MAX_HOST_CONNECTIONS,
                MaximumHostConnectionCount
            ) != CURLM_OK) {
            curl_multi_cleanup(multiHandle);
            return false;
        }
        _MultiHandle.store(multiHandle, std::memory_order_release);
        if (!Start(1)) {
            _MultiHandle.store(nullptr, std::memory_order_release);
            curl_multi_cleanup(multiHandle);
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(_RequestMutex);
            _AcceptingRequests = true;
        }
        return true;
    }

    void Http::Release(FoundationApi* const foundation) {
        if (foundation != _Foundation) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_RequestMutex);
            _AcceptingRequests = false;
            for (const std::pair<const UInt64, HttpRequestState*>& entry : _Requests) {
                if (entry.second) {
                    entry.second->_CancellationRequested.store(true, std::memory_order_release);
                }
            }
        }
        RequestStop();
        WakeWorker();
        Wait();
        CURLM* const multiHandle = static_cast<CURLM*>(
            _MultiHandle.exchange(nullptr, std::memory_order_acq_rel)
        );
        if (multiHandle) {
            curl_multi_cleanup(multiHandle);
        }
        while (true) {
            std::function<void()> callback;
            {
                std::lock_guard<std::mutex> lock(_CallbackMutex);
                if (_Callbacks.empty()) {
                    break;
                }
                callback = std::move(_Callbacks.front());
                _Callbacks.pop_front();
            }
            if (callback) {
                try {
                    callback();
                }
                catch (...) {
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(_RequestMutex);
            _PendingRequests.clear();
            for (const std::pair<const UInt64, HttpRequestState*>& entry : _Requests) {
                delete entry.second;
            }
            _Requests.clear();
        }
        {
            std::lock_guard<std::mutex> lock(_CallbackMutex);
            _Callbacks.clear();
        }
        curl_global_cleanup();
        _Foundation = nullptr;
    }

    void Http::EarlyUpdate(FoundationApi* const foundation) {
        (void)foundation;
    }

    void Http::Update(FoundationApi* const foundation) {
        if (foundation != _Foundation) {
            return;
        }
        const auto endTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
        while (true) {
            std::function<void()> callback;
            {
                std::lock_guard<std::mutex> lock(_CallbackMutex);
                if (_Callbacks.empty()) {
                    return;
                }
                callback = std::move(_Callbacks.front());
                _Callbacks.pop_front();
            }
            if (callback) {
                try {
                    callback();
                }
                catch (...) {
                }
            }
            if (std::chrono::steady_clock::now() >= endTime) {
                return;
            }
        }
    }

    void Http::LaterUpdate(FoundationApi* const foundation) {
        (void)foundation;
    }

    HttpSubmitResult Http::Submit(const HttpRequest& request, const HttpCallbacks& callbacks) {
        HttpSubmitResult result;
        const bool validScheme = request._Url.starts_with("http://")
            || request._Url.starts_with("https://");
        bool validHeaders = true;
        for (const HttpHeader& header : request._Headers) {
            if (header._Name.empty()
                || header._Name.find_first_of("\r\n:") != std::string::npos
                || header._Value.find_first_of("\r\n") != std::string::npos) {
                validHeaders = false;
                break;
            }
        }
        if (!validScheme
            || !validHeaders
            || request._MaximumResponseSize == 0
            || request._ConnectTimeoutMilliseconds == 0
            || request._RequestTimeoutMilliseconds == 0
            || request._ConnectTimeoutMilliseconds > static_cast<UInt64>((std::numeric_limits<long>::max)())
            || request._RequestTimeoutMilliseconds > static_cast<UInt64>((std::numeric_limits<long>::max)())
            || request._InactivityTimeoutSeconds > static_cast<UInt64>((std::numeric_limits<long>::max)())
            || (!request._Body.empty()
                && (request._Method == eHttpMethod::Get || request._Method == eHttpMethod::Head))) {
            result._Status = eHttpSubmitStatus::InvalidRequest;
            return result;
        }

        std::lock_guard<std::mutex> lock(_RequestMutex);
        if (!_AcceptingRequests || RunningThreadCount() == 0) {
            result._Status = eHttpSubmitStatus::SystemUnavailable;
            return result;
        }
        UInt64 requestId = _NextRequestId++;
        if (requestId == 0) {
            requestId = _NextRequestId++;
        }
        HttpRequestState* state = nullptr;
        try {
            state = new HttpRequestState(requestId, request, callbacks);
        }
        catch (...) {
            result._Status = eHttpSubmitStatus::SystemUnavailable;
            return result;
        }
        try {
            if (!_Requests.emplace(requestId, state).second) {
                delete state;
                result._Status = eHttpSubmitStatus::SystemUnavailable;
                return result;
            }
        }
        catch (...) {
            delete state;
            result._Status = eHttpSubmitStatus::SystemUnavailable;
            return result;
        }
        try {
            _PendingRequests.emplace_back(state);
        }
        catch (...) {
            _Requests.erase(requestId);
            delete state;
            result._Status = eHttpSubmitStatus::SystemUnavailable;
            return result;
        }
        result._Status = eHttpSubmitStatus::Accepted;
        result._RequestId = requestId;
        WakeWorker();
        return result;
    }

    bool Http::Cancel(const UInt64 requestId) {
        if (requestId == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(_RequestMutex);
        const auto iterator = _Requests.find(requestId);
        if (iterator == _Requests.end() || !iterator->second) {
            return false;
        }
        iterator->second->_CancellationRequested.store(true, std::memory_order_release);
        WakeWorker();
        return true;
    }

    void Http::Run(const int threadIndex, void* const context) {
        (void)threadIndex;
        (void)context;
        CURLM* const multiHandle = static_cast<CURLM*>(
            _MultiHandle.load(std::memory_order_acquire)
        );
        if (!multiHandle) {
            return;
        }
        std::unordered_map<CURL*, CurlRequestContext*> activeTransfers;

        const auto completeRequest = [this](
            HttpRequestState* const state,
            HttpResponse response
        ) {
            if (!state) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(_RequestMutex);
                _Requests.erase(state->_RequestId);
            }
            std::function<void(const HttpResponse&)> callback =
                std::move(state->_Callbacks._Completed);
            delete state;
            if (callback) {
                PostCallback([callback, response = std::move(response)]() {
                    callback(response);
                });
            }
        };

        const auto completeTransfer = [&](
            CurlRequestContext* const transfer,
            const CURLcode code
        ) {
            if (!transfer || !transfer->_State) {
                return;
            }
            if (transfer->_EasyHandle) {
                long statusCode = 0;
                curl_easy_getinfo(
                    transfer->_EasyHandle,
                    CURLINFO_RESPONSE_CODE,
                    &statusCode
                );
                transfer->_Response._StatusCode =
                    static_cast<Int64>(statusCode);
                char* finalUrl = nullptr;
                curl_easy_getinfo(
                    transfer->_EasyHandle,
                    CURLINFO_EFFECTIVE_URL,
                    &finalUrl
                );
                if (finalUrl) {
                    transfer->_Response._FinalUrl = finalUrl;
                }
            }

            if (transfer->_State->_CancellationRequested.load(
                    std::memory_order_acquire
                )) {
                transfer->_Response._Error = {
                    eHttpErrorCode::Cancelled,
                    static_cast<Int64>(code),
                    "The HTTP request was cancelled."
                };
            }
            else if (transfer->_ResponseTooLarge) {
                transfer->_Response._Error = {
                    eHttpErrorCode::ResponseTooLarge,
                    static_cast<Int64>(code),
                    "The HTTP response exceeded the configured size limit."
                };
            }
            else if (!transfer->_ForcedErrorMessage.empty()) {
                transfer->_Response._Error = {
                    eHttpErrorCode::TransportFailure,
                    transfer->_ForcedPlatformCode,
                    transfer->_ForcedErrorMessage
                };
            }
            else if (code == CURLE_OPERATION_TIMEDOUT) {
                transfer->_Response._Error = {
                    eHttpErrorCode::Timeout,
                    static_cast<Int64>(code),
                    "The HTTP request timed out."
                };
            }
            else if (code != CURLE_OK) {
                transfer->_Response._Error = {
                    eHttpErrorCode::TransportFailure,
                    static_cast<Int64>(code),
                    transfer->_ErrorBuffer[0] != '\0'
                        ? transfer->_ErrorBuffer
                        : curl_easy_strerror(code)
                };
            }

            if (transfer->_AddedToMulti && transfer->_EasyHandle) {
                curl_multi_remove_handle(
                    multiHandle,
                    transfer->_EasyHandle
                );
            }
            if (transfer->_RequestHeaders) {
                curl_slist_free_all(transfer->_RequestHeaders);
            }
            if (transfer->_EasyHandle) {
                curl_easy_cleanup(transfer->_EasyHandle);
            }
            HttpRequestState* const state = transfer->_State;
            HttpResponse response = std::move(transfer->_Response);
            delete transfer;
            completeRequest(state, std::move(response));
        };

        const auto createTransfer = [&](
            HttpRequestState* const state
        ) -> CurlRequestContext* {
            CurlRequestContext* const transfer =
                new (std::nothrow) CurlRequestContext();
            if (!transfer) {
                HttpResponse response;
                response._RequestId = state ? state->_RequestId : 0;
                response._Error = {
                    eHttpErrorCode::SystemUnavailable,
                    0,
                    "The HTTP transfer state could not be allocated."
                };
                completeRequest(state, std::move(response));
                return nullptr;
            }
            transfer->_Owner = this;
            transfer->_State = state;
            transfer->_Response._RequestId = state->_RequestId;
            transfer->_EasyHandle = curl_easy_init();
            if (!transfer->_EasyHandle) {
                transfer->_ForcedErrorMessage =
                    "Unable to create a libcurl easy handle.";
                completeTransfer(transfer, CURLE_FAILED_INIT);
                return nullptr;
            }

            CURLcode setupCode = CURLE_OK;
            const auto setOption = [&setupCode](const CURLcode code) {
                if (setupCode == CURLE_OK && code != CURLE_OK) {
                    setupCode = code;
                }
            };
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_URL,
                state->_Request._Url.c_str()
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_NOSIGNAL,
                1L
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_FOLLOWLOCATION,
                state->_Request._FollowRedirects ? 1L : 0L
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_CONNECTTIMEOUT_MS,
                static_cast<long>(
                    state->_Request._ConnectTimeoutMilliseconds
                )
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_TIMEOUT_MS,
                static_cast<long>(
                    state->_Request._RequestTimeoutMilliseconds
                )
            ));
            if (state->_Request._InactivityTimeoutSeconds != 0) {
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_LOW_SPEED_LIMIT,
                    1L
                ));
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_LOW_SPEED_TIME,
                    static_cast<long>(
                        state->_Request._InactivityTimeoutSeconds
                    )
                ));
            }
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_WRITEFUNCTION,
                &OnBody
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_WRITEDATA,
                transfer
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_HEADERFUNCTION,
                &OnHeader
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_HEADERDATA,
                transfer
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_XFERINFOFUNCTION,
                &OnProgress
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_XFERINFODATA,
                transfer
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_NOPROGRESS,
                0L
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_ERRORBUFFER,
                transfer->_ErrorBuffer
            ));
            setOption(curl_easy_setopt(
                transfer->_EasyHandle,
                CURLOPT_PRIVATE,
                transfer
            ));

            switch (state->_Request._Method) {
            case eHttpMethod::Get:
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_HTTPGET,
                    1L
                ));
                break;
            case eHttpMethod::Post:
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_POST,
                    1L
                ));
                break;
            case eHttpMethod::Put:
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_CUSTOMREQUEST,
                    "PUT"
                ));
                break;
            case eHttpMethod::Patch:
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_CUSTOMREQUEST,
                    "PATCH"
                ));
                break;
            case eHttpMethod::Delete:
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_CUSTOMREQUEST,
                    "DELETE"
                ));
                break;
            case eHttpMethod::Head:
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_NOBODY,
                    1L
                ));
                break;
            }
            if (!state->_Request._Body.empty()) {
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_POSTFIELDS,
                    state->_Request._Body.data()
                ));
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_POSTFIELDSIZE_LARGE,
                    static_cast<curl_off_t>(
                        state->_Request._Body.size()
                    )
                ));
            }
            for (const HttpHeader& header : state->_Request._Headers) {
                const std::string value =
                    header._Name + ": " + header._Value;
                curl_slist* const requestHeaders = curl_slist_append(
                    transfer->_RequestHeaders,
                    value.c_str()
                );
                if (!requestHeaders) {
                    setupCode = CURLE_OUT_OF_MEMORY;
                    break;
                }
                transfer->_RequestHeaders = requestHeaders;
            }
            if (transfer->_RequestHeaders) {
                setOption(curl_easy_setopt(
                    transfer->_EasyHandle,
                    CURLOPT_HTTPHEADER,
                    transfer->_RequestHeaders
                ));
            }
            if (setupCode != CURLE_OK) {
                transfer->_ForcedPlatformCode =
                    static_cast<Int64>(setupCode);
                transfer->_ForcedErrorMessage =
                    curl_easy_strerror(setupCode);
                completeTransfer(transfer, setupCode);
                return nullptr;
            }

            const CURLMcode addCode = curl_multi_add_handle(
                multiHandle,
                transfer->_EasyHandle
            );
            if (addCode != CURLM_OK) {
                transfer->_ForcedPlatformCode =
                    static_cast<Int64>(addCode);
                transfer->_ForcedErrorMessage =
                    curl_multi_strerror(addCode);
                completeTransfer(transfer, CURLE_FAILED_INIT);
                return nullptr;
            }
            transfer->_AddedToMulti = true;
            try {
                activeTransfers.emplace(
                    transfer->_EasyHandle,
                    transfer
                );
            }
            catch (...) {
                transfer->_ForcedErrorMessage =
                    "The active HTTP transfer could not be registered.";
                completeTransfer(transfer, CURLE_OUT_OF_MEMORY);
                return nullptr;
            }
            return transfer;
        };

        const auto takePendingRequests = [this]() {
            std::vector<HttpRequestState*> requests;
            std::lock_guard<std::mutex> lock(_RequestMutex);
            requests.reserve(_PendingRequests.size());
            while (!_PendingRequests.empty()) {
                requests.push_back(_PendingRequests.front());
                _PendingRequests.pop_front();
            }
            return requests;
        };

        const auto failMulti = [&](
            const CURLMcode code,
            const char* const operation
        ) {
            const std::string message =
                std::string(operation ? operation : "libcurl multi")
                + " failed: " + curl_multi_strerror(code);
            for (const std::pair<CURL* const, CurlRequestContext*>& entry
                : activeTransfers) {
                CurlRequestContext* const transfer = entry.second;
                transfer->_ForcedPlatformCode = static_cast<Int64>(code);
                transfer->_ForcedErrorMessage = message;
                completeTransfer(transfer, CURLE_FAILED_INIT);
            }
            activeTransfers.clear();
            for (HttpRequestState* const state : takePendingRequests()) {
                HttpResponse response;
                response._RequestId = state ? state->_RequestId : 0;
                response._Error = {
                    eHttpErrorCode::SystemUnavailable,
                    static_cast<Int64>(code),
                    message
                };
                completeRequest(state, std::move(response));
            }
            std::lock_guard<std::mutex> lock(_RequestMutex);
            _AcceptingRequests = false;
        };

        while (true) {
            for (HttpRequestState* const state : takePendingRequests()) {
                if (!state) {
                    continue;
                }
                if (state->_CancellationRequested.load(
                        std::memory_order_acquire
                    )) {
                    HttpResponse response;
                    response._RequestId = state->_RequestId;
                    response._Error = {
                        eHttpErrorCode::Cancelled,
                        static_cast<Int64>(CURLE_ABORTED_BY_CALLBACK),
                        "The HTTP request was cancelled."
                    };
                    completeRequest(state, std::move(response));
                    continue;
                }
                createTransfer(state);
            }

            for (auto iterator = activeTransfers.begin();
                iterator != activeTransfers.end();) {
                CurlRequestContext* const transfer = iterator->second;
                if (!transfer
                    || !transfer->_State
                    || !transfer->_State->_CancellationRequested.load(
                        std::memory_order_acquire
                    )) {
                    ++iterator;
                    continue;
                }
                iterator = activeTransfers.erase(iterator);
                completeTransfer(
                    transfer,
                    CURLE_ABORTED_BY_CALLBACK
                );
            }

            int runningHandleCount = 0;
            const CURLMcode performCode = curl_multi_perform(
                multiHandle,
                &runningHandleCount
            );
            if (performCode != CURLM_OK) {
                failMulti(performCode, "curl_multi_perform");
                return;
            }

            int remainingMessageCount = 0;
            while (CURLMsg* const message = curl_multi_info_read(
                multiHandle,
                &remainingMessageCount
            )) {
                if (message->msg != CURLMSG_DONE
                    || !message->easy_handle) {
                    continue;
                }
                const auto iterator = activeTransfers.find(
                    message->easy_handle
                );
                if (iterator == activeTransfers.end()) {
                    continue;
                }
                CurlRequestContext* const transfer = iterator->second;
                activeTransfers.erase(iterator);
                completeTransfer(transfer, message->data.result);
            }

            if (IsStopRequested() && activeTransfers.empty()) {
                const std::vector<HttpRequestState*> pendingRequests =
                    takePendingRequests();
                if (pendingRequests.empty()) {
                    return;
                }
                for (HttpRequestState* const state : pendingRequests) {
                    HttpResponse response;
                    response._RequestId = state ? state->_RequestId : 0;
                    response._Error = {
                        eHttpErrorCode::Cancelled,
                        static_cast<Int64>(CURLE_ABORTED_BY_CALLBACK),
                        "The HTTP request was cancelled."
                    };
                    completeRequest(state, std::move(response));
                }
                return;
            }

            int descriptorCount = 0;
            const CURLMcode pollCode = curl_multi_poll(
                multiHandle,
                nullptr,
                0,
                1000,
                &descriptorCount
            );
            if (pollCode != CURLM_OK) {
                failMulti(pollCode, "curl_multi_poll");
                return;
            }
        }
    }

    void Http::WakeWorker() {
        CURLM* const multiHandle = static_cast<CURLM*>(
            _MultiHandle.load(std::memory_order_acquire)
        );
        if (multiHandle) {
            curl_multi_wakeup(multiHandle);
        }
    }

    void Http::PostCallback(std::function<void()> callback) {
        if (!callback) {
            return;
        }
        try {
            std::lock_guard<std::mutex> lock(_CallbackMutex);
            _Callbacks.emplace_back(std::move(callback));
        }
        catch (...) {
        }
    }
}

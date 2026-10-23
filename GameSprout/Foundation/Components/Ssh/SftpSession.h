#ifndef SFTP_SESSION_IMPLEMENTATION_H
#define SFTP_SESSION_IMPLEMENTATION_H

#include "Header.h"

#include <chrono>

namespace Foundation {
    class SshRoute;

    class SftpSession final : public ISftpSession {
    public:
        SftpSession(
            const SshConnectOptions& options,
            const ISshApi::fSftpConnect& connectCallback);
        ~SftpSession() override;

        SftpSession(const SftpSession&) = delete;
        SftpSession& operator=(const SftpSession&) = delete;

        bool Start(INetApi* netApi, ITimerApi* timerApi);
        bool CanDestroy() const;

        void RegisterEvent(
            eSftpSessionEvent event,
            const fSftpSessionEvent& callback) override;
        bool Upload(
            SftpUploadRequest request,
            const fSftpUploadComplete& callback,
            const fSftpUploadProgress& progressCallback) override;
        void Close() override;

    private:
        enum class eSftpState {
            Created,
            InitializingSftp,
            Connected,
            OpeningRemoteFile,
            WritingRemoteFile,
            ClosingRemoteFile,
            Closing,
            Closed,
            Failed
        };

        void OnRouteConnected(
            bool succeed,
            eSshConnectError error,
            const std::string& message);
        void OnRouteDisconnected();
        void OnConnectTimeout();
        void DriveStateMachine();
        void NotifyEvent(eSftpSessionEvent event);
        void NotifyUploadProgress(bool force = false);
        void CompleteConnect(
            bool success,
            eSshConnectError error,
            const std::string& message);
        void CompleteUpload(
            bool success,
            eSftpTransferError error,
            const std::string& message);
        void FailConnect(eSshConnectError error, const std::string& message);
        void FailUpload(
            eSftpTransferError error,
            const std::string& message);
        void CleanupSftpHandles();
        std::string GetSessionError(const std::string& fallback) const;
        std::string GetSftpError(const std::string& fallback) const;

        static constexpr std::size_t s_EventCount = 2;
        static constexpr std::size_t s_UploadWriteWindowSize = 4 * 1024 * 1024;
        static constexpr std::size_t s_DriveAcknowledgedByteBudget =
            4 * 1024 * 1024;
        static constexpr int s_DriveOperationBudget = 256;
        static constexpr std::chrono::milliseconds s_DriveTimeBudget{2};
        static constexpr std::size_t s_ProgressByteInterval = 256 * 1024;
        static constexpr std::chrono::milliseconds s_ProgressTimeInterval{250};

        SshConnectOptions _Options;
        ISshApi::fSftpConnect _ConnectCallback;
        std::array<std::vector<fSftpSessionEvent>, s_EventCount> _Events;
        SftpUploadRequest _UploadRequest;
        fSftpUploadComplete _UploadCallback;
        fSftpUploadProgress _UploadProgressCallback;
        std::size_t _UploadOffset = 0;
        std::size_t _LastUploadProgressOffset = 0;
        std::chrono::steady_clock::time_point _LastUploadProgressTime;
        SshRoute* _Route = nullptr;
        LIBSSH2_SFTP* _Sftp = nullptr;
        LIBSSH2_SFTP_HANDLE* _RemoteFile = nullptr;
        eSftpState _State = eSftpState::Created;
        bool _ConnectCallbackCalled = false;
        bool _Connected = false;
        bool _DisconnectEventCalled = false;
        bool _UploadActive = false;
        bool _UploadProgressReported = false;
        bool _CanDestroy = false;
        bool _Driving = false;
        bool _DrivePending = false;
    };
}

#endif // SFTP_SESSION_IMPLEMENTATION_H

#include "SftpSession.h"

#include "SshRoute.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace Foundation {
    SftpSession::SftpSession(
        const SshConnectOptions& options,
        const ISshApi::fSftpConnect& connectCallback)
        : _Options(options),
        _ConnectCallback(connectCallback) {
    }

    SftpSession::~SftpSession() {
        CleanupSftpHandles();
        if (_Route) {
            _Route->Close();
            delete _Route;
            _Route = nullptr;
        }
    }

    bool SftpSession::Start(INetApi* const netApi, ITimerApi* const timerApi) {
        if (_State != eSftpState::Created || !netApi || !timerApi || _Route) {
            return false;
        }

        _Route = new SshRoute(
            std::move(_Options),
            [this](
                const bool succeed,
                const eSshConnectError error,
                const std::string& message) {
                OnRouteConnected(succeed, error, message);
            },
            [this]() {
                DriveStateMachine();
            },
            [this]() {
                OnConnectTimeout();
            },
            [this]() {
                OnRouteDisconnected();
            });
        if (!_Route || !_Route->Start(netApi, timerApi)) {
            OnRouteConnected(
                false,
                eSshConnectError::TcpConnectFailed,
                "Failed to start SFTP SSH route.");
            return false;
        }
        return true;
    }

    bool SftpSession::CanDestroy() const {
        const bool terminal = _State == eSftpState::Closed
            || _State == eSftpState::Failed;
        return terminal && (!_Route || _Route->CanDestroy());
    }

    void SftpSession::RegisterEvent(
        const eSftpSessionEvent event,
        const fSftpSessionEvent& callback) {
        const std::size_t eventIndex = static_cast<std::size_t>(event);
        if (!callback || eventIndex >= _Events.size()) {
            return;
        }
        _Events[eventIndex].push_back(callback);
    }

    bool SftpSession::Upload(
        SftpUploadRequest request,
        const fSftpUploadComplete& callback,
        const fSftpUploadProgress& progressCallback) {
        if (_State != eSftpState::Connected
            || _UploadActive
            || request._RemotePath.empty()
            || !callback) {
            return false;
        }
        _UploadRequest = std::move(request);
        _UploadCallback = callback;
        _UploadProgressCallback = progressCallback;
        _UploadOffset = 0;
        _LastUploadProgressOffset = 0;
        _LastUploadProgressTime = std::chrono::steady_clock::now();
        _UploadProgressReported = false;
        _UploadActive = true;
        _State = eSftpState::OpeningRemoteFile;
        DriveStateMachine();
        return true;
    }

    void SftpSession::Close() {
        if (_State == eSftpState::Closed || _State == eSftpState::Failed) {
            if (_Route) {
                _Route->Close();
            }
            return;
        }

        _State = eSftpState::Closing;
        CompleteUpload(
            false,
            eSftpTransferError::Cancelled,
            "SFTP upload was cancelled.");
        CompleteConnect(
            false,
            eSshConnectError::Cancelled,
            "SFTP session was cancelled.");
        CleanupSftpHandles();
        if (_Route) {
            _Route->Close();
            if (_Route->CanDestroy()) {
                _State = eSftpState::Closed;
            }
        }
        else {
            _State = eSftpState::Closed;
        }
    }

    void SftpSession::OnRouteConnected(
        const bool succeed,
        const eSshConnectError error,
        const std::string& message) {
        if (!succeed) {
            if (_State != eSftpState::Closing && _State != eSftpState::Closed) {
                _State = eSftpState::Failed;
            }
            CompleteConnect(false, error, message);
            return;
        }
        if (_State != eSftpState::Created || !_Route || !_Route->TargetSession()) {
            FailConnect(
                eSshConnectError::InternalError,
                "SFTP route completed without a target SSH connection.");
            return;
        }
        _State = eSftpState::InitializingSftp;
        DriveStateMachine();
    }

    void SftpSession::OnRouteDisconnected() {
        const bool wasConnected = _Connected;
        CompleteUpload(
            false,
            eSftpTransferError::ConnectionClosed,
            "SFTP SSH route was disconnected.");
        CleanupSftpHandles();
        _Connected = false;
        _State = eSftpState::Closed;
        CompleteConnect(
            false,
            eSshConnectError::TcpConnectFailed,
            "SFTP SSH route was disconnected.");
        if (wasConnected && !_DisconnectEventCalled) {
            _DisconnectEventCalled = true;
            NotifyEvent(eSftpSessionEvent::Disconnected);
        }
    }

    void SftpSession::OnConnectTimeout() {
        const std::string stage = _State == eSftpState::InitializingSftp
            ? "initializing the SFTP subsystem"
            : "establishing the SSH route";
        FailConnect(
            eSshConnectError::Timeout,
            "SFTP connection timed out while " + stage + ".");
    }

    void SftpSession::DriveStateMachine() {
        if (_Driving) {
            _DrivePending = true;
            return;
        }
        if (!_Route || !_Route->TargetSession()) {
            return;
        }

        const auto driveStartedAt = std::chrono::steady_clock::now();
        std::size_t acknowledgedBytes = 0;
        int operations = 0;
        bool budgetExhausted = false;
        do {
            _DrivePending = false;
            _Driving = true;
            bool continueDriving = true;
            LIBSSH2_SESSION* const session = _Route->TargetSession();

            while (continueDriving) {
                if (operations >= s_DriveOperationBudget
                    || acknowledgedBytes >= s_DriveAcknowledgedByteBudget
                    || (operations > 0
                        && std::chrono::steady_clock::now() - driveStartedAt
                            >= s_DriveTimeBudget)) {
                    budgetExhausted = true;
                    break;
                }
                ++operations;
                switch (_State) {
                case eSftpState::InitializingSftp:
                    _Sftp = libssh2_sftp_init(session);
                    if (!_Sftp) {
                        if (libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
                            _Route->HandleWouldBlock();
                            continueDriving = false;
                        }
                        else {
                            FailConnect(
                                eSshConnectError::ChannelFailed,
                                GetSessionError("Failed to initialize SFTP subsystem."));
                            continueDriving = false;
                        }
                    }
                    else {
                        _Route->NotifyProgress();
                        _State = eSftpState::Connected;
                        _Connected = true;
                        _Route->CompleteConnectionSetup();
                        CompleteConnect(
                            true,
                            eSshConnectError::None,
                            std::string());
                        if (_State == eSftpState::Connected) {
                            NotifyEvent(eSftpSessionEvent::Connected);
                        }
                    }
                    break;
                case eSftpState::OpeningRemoteFile:
                    _RemoteFile = libssh2_sftp_open_ex(
                        _Sftp,
                        _UploadRequest._RemotePath.c_str(),
                        static_cast<unsigned int>(_UploadRequest._RemotePath.size()),
                        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
                        static_cast<long>(_UploadRequest._Permissions),
                        LIBSSH2_SFTP_OPENFILE);
                    if (!_RemoteFile) {
                        if (libssh2_session_last_errno(session) == LIBSSH2_ERROR_EAGAIN) {
                            _Route->HandleWouldBlock();
                            continueDriving = false;
                        }
                        else {
                            FailUpload(
                                eSftpTransferError::OpenFailed,
                                GetSftpError("Failed to open remote SFTP file."));
                            continueDriving = false;
                        }
                    }
                    else {
                        _Route->NotifyProgress();
                        _State = eSftpState::WritingRemoteFile;
                    }
                    break;
                case eSftpState::WritingRemoteFile: {
                    if (_UploadOffset >= _UploadRequest._Content.size()) {
                        _State = eSftpState::ClosingRemoteFile;
                        break;
                    }
                    const std::size_t remaining =
                        _UploadRequest._Content.size() - _UploadOffset;
                    const std::size_t writeSize =
                        (std::min)(remaining, s_UploadWriteWindowSize);
                    const char* const data = reinterpret_cast<const char*>(
                        _UploadRequest._Content.data() + _UploadOffset);
                    const ssize_t result = libssh2_sftp_write(
                        _RemoteFile,
                        data,
                        writeSize);
                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        _Route->HandleWouldBlock();
                        continueDriving = false;
                    }
                    else if (result < 0) {
                        FailUpload(
                            eSftpTransferError::WriteFailed,
                            GetSftpError("Failed to write remote SFTP file."));
                        continueDriving = false;
                    }
                    else if (result == 0) {
                        _Route->HandleWouldBlock();
                        continueDriving = false;
                    }
                    else {
                        _Route->NotifyProgress();
                        const std::size_t written =
                            static_cast<std::size_t>(result);
                        _UploadOffset += written;
                        acknowledgedBytes += written;
                        NotifyUploadProgress();
                        if (_State != eSftpState::WritingRemoteFile) {
                            continueDriving = false;
                        }
                    }
                    break;
                }
                case eSftpState::ClosingRemoteFile: {
                    const int result = libssh2_sftp_close_handle(_RemoteFile);
                    if (result == LIBSSH2_ERROR_EAGAIN) {
                        _Route->HandleWouldBlock();
                        continueDriving = false;
                        break;
                    }
                    _RemoteFile = nullptr;
                    if (result != 0) {
                        FailUpload(
                            eSftpTransferError::CloseFailed,
                            GetSftpError("Failed to close remote SFTP file."));
                    }
                    else {
                        _Route->NotifyProgress();
                        NotifyUploadProgress(true);
                        if (_State != eSftpState::ClosingRemoteFile
                            || !_UploadActive) {
                            continueDriving = false;
                            break;
                        }
                        _State = eSftpState::Connected;
                        CompleteUpload(
                            true,
                            eSftpTransferError::None,
                            std::string());
                    }
                    continueDriving = false;
                    break;
                }
                default:
                    continueDriving = false;
                    break;
                }
            }
            _Driving = false;
        } while (!budgetExhausted
            && _DrivePending
            && _Route
            && _Route->TargetSession());

        if (budgetExhausted
            && _Route
            && _Route->TargetSession()
            && (_State == eSftpState::InitializingSftp
                || _State == eSftpState::OpeningRemoteFile
                || _State == eSftpState::WritingRemoteFile
                || _State == eSftpState::ClosingRemoteFile)) {
            _Route->RequestActivity();
        }
    }

    void SftpSession::NotifyEvent(const eSftpSessionEvent event) {
        const std::size_t eventIndex = static_cast<std::size_t>(event);
        if (eventIndex >= _Events.size()) {
            return;
        }
        const std::vector<fSftpSessionEvent> callbacks = _Events[eventIndex];
        for (const fSftpSessionEvent& callback : callbacks) {
            try {
                callback(this);
            }
            catch (...) {
            }
        }
    }

    void SftpSession::NotifyUploadProgress(const bool force) {
        if (!_UploadProgressCallback) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const std::size_t totalBytes = _UploadRequest._Content.size();
        const bool completed = _UploadOffset >= totalBytes;
        if (_UploadProgressReported
            && _LastUploadProgressOffset == _UploadOffset
            && (force || completed)) {
            return;
        }
        const std::size_t bytesSinceLastProgress =
            _UploadOffset - _LastUploadProgressOffset;
        if (!force
            && !completed
            && bytesSinceLastProgress < s_ProgressByteInterval
            && now - _LastUploadProgressTime < s_ProgressTimeInterval) {
            return;
        }

        const fSftpUploadProgress callback = _UploadProgressCallback;
        _LastUploadProgressOffset = _UploadOffset;
        _LastUploadProgressTime = now;
        _UploadProgressReported = true;
        try {
            callback(
                this,
                static_cast<std::uint64_t>(_UploadOffset),
                static_cast<std::uint64_t>(totalBytes));
        }
        catch (...) {
        }
    }

    void SftpSession::CompleteConnect(
        const bool success,
        const eSshConnectError error,
        const std::string& message) {
        if (_ConnectCallbackCalled) {
            return;
        }
        _ConnectCallbackCalled = true;
        const ISshApi::fSftpConnect callback = std::move(_ConnectCallback);
        _ConnectCallback = nullptr;
        if (!callback) {
            return;
        }
        try {
            callback(success, success ? this : nullptr, error, message);
        }
        catch (...) {
        }
    }

    void SftpSession::CompleteUpload(
        const bool success,
        const eSftpTransferError error,
        const std::string& message) {
        if (!_UploadActive) {
            return;
        }
        const std::uint64_t transferredBytes =
            static_cast<std::uint64_t>(_UploadOffset);
        const fSftpUploadComplete callback = std::move(_UploadCallback);
        _UploadRequest = {};
        _UploadCallback = nullptr;
        _UploadProgressCallback = nullptr;
        _UploadOffset = 0;
        _LastUploadProgressOffset = 0;
        _LastUploadProgressTime = {};
        _UploadProgressReported = false;
        _UploadActive = false;
        if (!callback) {
            return;
        }
        try {
            callback(this, success, error, transferredBytes, message);
        }
        catch (...) {
        }
    }

    void SftpSession::FailConnect(
        const eSshConnectError error,
        const std::string& message) {
        if (_State == eSftpState::Failed || _State == eSftpState::Closed) {
            return;
        }
        _State = eSftpState::Failed;
        CompleteUpload(
            false,
            eSftpTransferError::ConnectionClosed,
            message);
        CleanupSftpHandles();
        CompleteConnect(false, error, message);
        if (_Route) {
            _Route->Close();
        }
    }

    void SftpSession::FailUpload(
        const eSftpTransferError error,
        const std::string& message) {
        if (_RemoteFile) {
            (void)libssh2_sftp_close_handle(_RemoteFile);
            _RemoteFile = nullptr;
        }
        _State = eSftpState::Connected;
        if (_Route) {
            _Route->NotifyProgress();
        }
        CompleteUpload(false, error, message);
    }

    void SftpSession::CleanupSftpHandles() {
        if (_RemoteFile) {
            (void)libssh2_sftp_close_handle(_RemoteFile);
            _RemoteFile = nullptr;
        }
        if (_Sftp) {
            (void)libssh2_sftp_shutdown(_Sftp);
            _Sftp = nullptr;
        }
    }

    std::string SftpSession::GetSessionError(
        const std::string& fallback) const {
        LIBSSH2_SESSION* const session = _Route ? _Route->TargetSession() : nullptr;
        if (!session) {
            return fallback;
        }
        char* errorMessage = nullptr;
        int errorLength = 0;
        libssh2_session_last_error(
            session,
            &errorMessage,
            &errorLength,
            0);
        if (!errorMessage || errorLength <= 0) {
            return fallback;
        }
        return std::string(errorMessage, static_cast<std::size_t>(errorLength));
    }

    std::string SftpSession::GetSftpError(const std::string& fallback) const {
        if (!_Sftp) {
            return GetSessionError(fallback);
        }
        const unsigned long error = libssh2_sftp_last_error(_Sftp);
        if (error == 0) {
            return GetSessionError(fallback);
        }
        return fallback + " SFTP error=" + std::to_string(error) + ".";
    }
}

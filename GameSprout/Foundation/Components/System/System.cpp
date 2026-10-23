#include "System.h"
#include "SystemFileWatchers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdio>
#include <deque>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <cwctype>
#else
#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#endif

namespace Foundation {
    namespace {
#if defined(_WIN32)
        using SocketHandle = SOCKET;
        constexpr SocketHandle InvalidSocketHandle = INVALID_SOCKET;
#else
        using SocketHandle = int;
        constexpr SocketHandle InvalidSocketHandle = -1;
#endif

        void SetError(
            SystemError* const error,
            const eSystemErrorCode code,
            const Int64 platformCode,
            std::string message
        ) {
            if (!error) {
                return;
            }
            error->_Code = code;
            error->_PlatformCode = platformCode;
            error->_Message = std::move(message);
        }

        Int64 LastSocketError() {
#if defined(_WIN32)
            return static_cast<Int64>(WSAGetLastError());
#else
            return static_cast<Int64>(errno);
#endif
        }

        bool SetSocketReceiveTimeout(const SocketHandle socket, const UInt64 timeoutMilliseconds) {
#if defined(_WIN32)
            const DWORD timeout = static_cast<DWORD>(timeoutMilliseconds);
            return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
            timeval timeout{};
            timeout.tv_sec = static_cast<time_t>(timeoutMilliseconds / 1000);
            timeout.tv_usec = static_cast<suseconds_t>((timeoutMilliseconds % 1000) * 1000);
            return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
        }

        void CloseSocket(SocketHandle& socket) {
            if (socket == InvalidSocketHandle) {
                return;
            }
#if defined(_WIN32)
            shutdown(socket, SD_BOTH);
            closesocket(socket);
#else
            shutdown(socket, SHUT_RDWR);
            close(socket);
#endif
            socket = InvalidSocketHandle;
        }

        bool SendAll(const SocketHandle socket, const void* const data, const std::size_t size) {
            const char* cursor = static_cast<const char*>(data);
            std::size_t remaining = size;
            while (remaining > 0) {
                const int chunkSize = static_cast<int>(
                    (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                );
#if defined(_WIN32)
                const int sent = send(socket, cursor, chunkSize, 0);
#else
                const int sent = static_cast<int>(send(socket, cursor, static_cast<std::size_t>(chunkSize), MSG_NOSIGNAL));
#endif
                if (sent <= 0) {
                    return false;
                }
                cursor += sent;
                remaining -= static_cast<std::size_t>(sent);
            }
            return true;
        }

        bool ReceiveAll(const SocketHandle socket, void* const data, const std::size_t size) {
            char* cursor = static_cast<char*>(data);
            std::size_t remaining = size;
            while (remaining > 0) {
                const int chunkSize = static_cast<int>(
                    (std::min)(remaining, static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                );
                const int received = recv(socket, cursor, chunkSize, 0);
                if (received <= 0) {
                    return false;
                }
                cursor += received;
                remaining -= static_cast<std::size_t>(received);
            }
            return true;
        }

        bool SendFrame(
            const SocketHandle socket,
            const void* const data,
            const std::size_t size
        ) {
            if (size > SystemIpcMaximumMessageSize) {
                return false;
            }
            const UInt32 networkSize = htonl(static_cast<UInt32>(size));
            return SendAll(socket, &networkSize, sizeof(networkSize))
                && (size == 0 || SendAll(socket, data, size));
        }

        bool ReceiveFrame(SocketHandle socket, std::vector<UInt8>& data) {
            UInt32 networkSize = 0;
            if (!ReceiveAll(socket, &networkSize, sizeof(networkSize))) {
                return false;
            }
            const std::size_t size = static_cast<std::size_t>(ntohl(networkSize));
            if (size > SystemIpcMaximumMessageSize) {
                return false;
            }
            try {
                data.resize(size);
            }
            catch (...) {
                return false;
            }
            return size == 0 || ReceiveAll(socket, data.data(), size);
        }

        std::string CreateAccessToken() {
            std::array<UInt8, 32> bytes{};
            std::random_device randomDevice;
            for (UInt8& byte : bytes) {
                byte = static_cast<UInt8>(randomDevice());
            }
            static constexpr char s_Hex[] = "0123456789abcdef";
            std::string token;
            token.reserve(bytes.size() * 2);
            for (const UInt8 byte : bytes) {
                token.push_back(s_Hex[(byte >> 4) & 0x0f]);
                token.push_back(s_Hex[byte & 0x0f]);
            }
            return token;
        }

#if defined(_WIN32)
        std::wstring Utf8ToWide(const std::string& value) {
            if (value.empty()) {
                return {};
            }
            const int length = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0
            );
            if (length <= 0) {
                return {};
            }
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                length
            );
            return result;
        }

        std::string WideToUtf8(const wchar_t* const value, const int length) {
            if (!value || length <= 0) {
                return {};
            }
            const int size = WideCharToMultiByte(
                CP_UTF8,
                0,
                value,
                length,
                nullptr,
                0,
                nullptr,
                nullptr
            );
            if (size <= 0) {
                return {};
            }
            std::string result(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                value,
                length,
                result.data(),
                size,
                nullptr,
                nullptr
            );
            return result;
        }

        std::string FormatWindowsError(const DWORD platformCode) {
            wchar_t* messageBuffer = nullptr;
            const DWORD messageLength = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER
                    | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                platformCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<wchar_t*>(&messageBuffer),
                0,
                nullptr
            );
            std::string message = WideToUtf8(
                messageBuffer,
                static_cast<int>(messageLength)
            );
            if (messageBuffer) {
                LocalFree(messageBuffer);
            }
            while (!message.empty()
                && (message.back() == '\r' || message.back() == '\n')) {
                message.pop_back();
            }
            return message;
        }

        std::string BuildWindowsProcessError(
            const char* const operation,
            const SystemProcessStartInfo& startInfo,
            const DWORD platformCode
        ) {
            std::ostringstream stream;
            stream
                << operation
                << " Executable: \"" << startInfo._Executable << "\"."
                << " Working directory: \""
                << (startInfo._WorkingDirectory.empty()
                    ? "<inherited>"
                    : startInfo._WorkingDirectory)
                << "\"."
                << " Win32 error " << platformCode;
            const std::string platformMessage =
                FormatWindowsError(platformCode);
            if (!platformMessage.empty()) {
                stream << ": " << platformMessage;
            }
            return stream.str();
        }

        std::wstring QuoteWindowsArgument(const std::wstring& argument) {
            if (argument.empty()) {
                return L"\"\"";
            }
            if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
                return argument;
            }
            std::wstring result = L"\"";
            std::size_t slashCount = 0;
            for (const wchar_t character : argument) {
                if (character == L'\\') {
                    ++slashCount;
                    continue;
                }
                if (character == L'"') {
                    result.append(slashCount * 2 + 1, L'\\');
                    result.push_back(L'"');
                    slashCount = 0;
                    continue;
                }
                result.append(slashCount, L'\\');
                slashCount = 0;
                result.push_back(character);
            }
            result.append(slashCount * 2, L'\\');
            result.push_back(L'"');
            return result;
        }
#else
        std::string BuildPosixProcessError(
            const char* const operation,
            const SystemProcessStartInfo& startInfo,
            const int platformCode
        ) {
            std::ostringstream stream;
            stream
                << operation
                << " Executable: \"" << startInfo._Executable << "\"."
                << " Working directory: \""
                << (startInfo._WorkingDirectory.empty()
                    ? "<inherited>"
                    : startInfo._WorkingDirectory)
                << "\"."
                << " POSIX error " << platformCode;
            if (platformCode != 0) {
                stream << ": " << std::strerror(platformCode);
            }
            return stream.str();
        }
#endif
    }

    class SystemProcess final : public ISystemProcess {
    public:
        SystemProcess(System* const owner, SystemProcessCallbacks callbacks)
            : _Owner(owner)
            , _Callbacks(std::move(callbacks)) {
        }

        ~SystemProcess() override {
            Close();
        }

        bool Start(const SystemProcessStartInfo& startInfo, SystemError* const error) {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE;

            HANDLE standardOutputRead = nullptr;
            HANDLE standardOutputWrite = nullptr;
            HANDLE standardErrorRead = nullptr;
            HANDLE standardErrorWrite = nullptr;
            if (startInfo._CaptureOutput) {
                if (!CreatePipe(&standardOutputRead, &standardOutputWrite, &securityAttributes, 0)
                    || !SetHandleInformation(standardOutputRead, HANDLE_FLAG_INHERIT, 0)
                    || !CreatePipe(&standardErrorRead, &standardErrorWrite, &securityAttributes, 0)
                    || !SetHandleInformation(standardErrorRead, HANDLE_FLAG_INHERIT, 0)) {
                    const DWORD platformCode = GetLastError();
                    if (standardOutputRead) {
                        CloseHandle(standardOutputRead);
                    }
                    if (standardOutputWrite) {
                        CloseHandle(standardOutputWrite);
                    }
                    if (standardErrorRead) {
                        CloseHandle(standardErrorRead);
                    }
                    if (standardErrorWrite) {
                        CloseHandle(standardErrorWrite);
                    }
                    SetError(error, eSystemErrorCode::ProcessIoFailed, platformCode, "Failed to create process output pipes.");
                    return false;
                }
            }

            const std::wstring executable = Utf8ToWide(startInfo._Executable);
            if (executable.empty()) {
                if (standardOutputRead) {
                    CloseHandle(standardOutputRead);
                }
                if (standardOutputWrite) {
                    CloseHandle(standardOutputWrite);
                }
                if (standardErrorRead) {
                    CloseHandle(standardErrorRead);
                }
                if (standardErrorWrite) {
                    CloseHandle(standardErrorWrite);
                }
                SetError(error, eSystemErrorCode::InvalidArgument, GetLastError(), "The process executable is not valid UTF-8.");
                return false;
            }

            std::wstring commandLine = QuoteWindowsArgument(executable);
            for (const std::string& argument : startInfo._Arguments) {
                const std::wstring wideArgument = Utf8ToWide(argument);
                if (!argument.empty() && wideArgument.empty()) {
                    if (standardOutputRead) {
                        CloseHandle(standardOutputRead);
                    }
                    if (standardOutputWrite) {
                        CloseHandle(standardOutputWrite);
                    }
                    if (standardErrorRead) {
                        CloseHandle(standardErrorRead);
                    }
                    if (standardErrorWrite) {
                        CloseHandle(standardErrorWrite);
                    }
                    SetError(error, eSystemErrorCode::InvalidArgument, GetLastError(), "A process argument is not valid UTF-8.");
                    return false;
                }
                commandLine.push_back(L' ');
                commandLine += QuoteWindowsArgument(wideArgument);
            }
            std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
            mutableCommandLine.push_back(L'\0');
            const std::wstring workingDirectory = Utf8ToWide(startInfo._WorkingDirectory);
            if (!startInfo._WorkingDirectory.empty() && workingDirectory.empty()) {
                if (standardOutputRead) {
                    CloseHandle(standardOutputRead);
                }
                if (standardOutputWrite) {
                    CloseHandle(standardOutputWrite);
                }
                if (standardErrorRead) {
                    CloseHandle(standardErrorRead);
                }
                if (standardErrorWrite) {
                    CloseHandle(standardErrorWrite);
                }
                SetError(error, eSystemErrorCode::InvalidArgument, GetLastError(), "The process working directory is not valid UTF-8.");
                return false;
            }

            std::map<std::wstring, std::pair<std::wstring, std::wstring>> environmentValues;
            wchar_t* const inheritedEnvironment = GetEnvironmentStringsW();
            if (!inheritedEnvironment) {
                SetError(error, eSystemErrorCode::ProcessLaunchFailed, GetLastError(), "Unable to read the process environment.");
                return false;
            }
            for (const wchar_t* entry = inheritedEnvironment; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
                const std::wstring value = entry;
                const std::size_t separator = value.find(L'=', value[0] == L'=' ? 1 : 0);
                if (separator == std::wstring::npos) {
                    continue;
                }
                const std::wstring name = value.substr(0, separator);
                std::wstring key = name;
                std::transform(key.begin(), key.end(), key.begin(), [](const wchar_t character) {
                    return static_cast<wchar_t>(std::towlower(character));
                });
                environmentValues[key] = {name, value.substr(separator + 1)};
            }
            FreeEnvironmentStringsW(inheritedEnvironment);
            for (const SystemProcessEnvironmentVariable& variable : startInfo._Environment) {
                const std::wstring name = Utf8ToWide(variable._Name);
                const std::wstring value = Utf8ToWide(variable._Value);
                if (name.empty() || name.find(L'=') != std::wstring::npos
                    || (!variable._Value.empty() && value.empty())) {
                    SetError(error, eSystemErrorCode::InvalidArgument, 0, "A child environment variable is invalid.");
                    return false;
                }
                std::wstring key = name;
                std::transform(key.begin(), key.end(), key.begin(), [](const wchar_t character) {
                    return static_cast<wchar_t>(std::towlower(character));
                });
                environmentValues[key] = {name, value};
            }
            std::vector<wchar_t> environmentBlock;
            for (const auto& pair : environmentValues) {
                const std::wstring entry = pair.second.first + L"=" + pair.second.second;
                environmentBlock.insert(environmentBlock.end(), entry.begin(), entry.end());
                environmentBlock.push_back(L'\0');
            }
            environmentBlock.push_back(L'\0');

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            if (startInfo._CaptureOutput) {
                startupInfo.dwFlags |= STARTF_USESTDHANDLES;
                startupInfo.hStdOutput = standardOutputWrite;
                startupInfo.hStdError = standardErrorWrite;
                startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            }
            PROCESS_INFORMATION processInformation{};
            DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT;
            if (startInfo._Hidden) {
                creationFlags |= CREATE_NO_WINDOW;
            }
            HANDLE ownerJob = nullptr;
            if (startInfo._TerminateWithOwner) {
                ownerJob = CreateJobObjectW(nullptr, nullptr);
                if (!ownerJob) {
                    if (standardOutputRead) {
                        CloseHandle(standardOutputRead);
                    }
                    if (standardOutputWrite) {
                        CloseHandle(standardOutputWrite);
                    }
                    if (standardErrorRead) {
                        CloseHandle(standardErrorRead);
                    }
                    if (standardErrorWrite) {
                        CloseHandle(standardErrorWrite);
                    }
                    SetError(
                        error,
                        eSystemErrorCode::ProcessLaunchFailed,
                        GetLastError(),
                        "CreateJobObjectW failed.");
                    return false;
                }
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limitInformation{};
                limitInformation.BasicLimitInformation.LimitFlags =
                    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!SetInformationJobObject(
                        ownerJob,
                        JobObjectExtendedLimitInformation,
                        &limitInformation,
                        sizeof(limitInformation))) {
                    const DWORD platformCode = GetLastError();
                    CloseHandle(ownerJob);
                    if (standardOutputRead) {
                        CloseHandle(standardOutputRead);
                    }
                    if (standardOutputWrite) {
                        CloseHandle(standardOutputWrite);
                    }
                    if (standardErrorRead) {
                        CloseHandle(standardErrorRead);
                    }
                    if (standardErrorWrite) {
                        CloseHandle(standardErrorWrite);
                    }
                    SetError(
                        error,
                        eSystemErrorCode::ProcessLaunchFailed,
                        platformCode,
                        "SetInformationJobObject failed.");
                    return false;
                }
                creationFlags |= CREATE_SUSPENDED;
            }

            HANDLE standardInput = nullptr;
            bool closeStandardInput = false;
            if (startInfo._CaptureOutput) {
                standardInput = GetStdHandle(STD_INPUT_HANDLE);
                if (!standardInput || standardInput == INVALID_HANDLE_VALUE) {
                    standardInput = CreateFileW(
                        L"NUL",
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        &securityAttributes,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        nullptr
                    );
                    if (standardInput == INVALID_HANDLE_VALUE) {
                        const DWORD platformCode = GetLastError();
                        if (ownerJob) {
                            CloseHandle(ownerJob);
                        }
                        if (standardOutputRead) {
                            CloseHandle(standardOutputRead);
                        }
                        if (standardOutputWrite) {
                            CloseHandle(standardOutputWrite);
                        }
                        if (standardErrorRead) {
                            CloseHandle(standardErrorRead);
                        }
                        if (standardErrorWrite) {
                            CloseHandle(standardErrorWrite);
                        }
                        SetError(
                            error,
                            eSystemErrorCode::ProcessIoFailed,
                            platformCode,
                            BuildWindowsProcessError(
                                "Unable to create a valid standard input handle.",
                                startInfo,
                                platformCode
                            )
                        );
                        return false;
                    }
                    closeStandardInput = true;
                }
                startupInfo.hStdInput = standardInput;
            }

            const BOOL created = CreateProcessW(
                executable.c_str(),
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                startInfo._CaptureOutput ? TRUE : FALSE,
                creationFlags,
                environmentBlock.data(),
                workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                &startupInfo,
                &processInformation
            );
            const DWORD createProcessPlatformCode =
                created ? ERROR_SUCCESS : GetLastError();
            if (standardOutputWrite) {
                CloseHandle(standardOutputWrite);
            }
            if (standardErrorWrite) {
                CloseHandle(standardErrorWrite);
            }
            if (closeStandardInput) {
                CloseHandle(standardInput);
            }
            if (!created) {
                const DWORD platformCode = createProcessPlatformCode;
                if (ownerJob) {
                    CloseHandle(ownerJob);
                }
                if (standardOutputRead) {
                    CloseHandle(standardOutputRead);
                }
                if (standardErrorRead) {
                    CloseHandle(standardErrorRead);
                }
                SetError(
                    error,
                    eSystemErrorCode::ProcessLaunchFailed,
                    platformCode,
                    BuildWindowsProcessError(
                        "CreateProcessW failed.",
                        startInfo,
                        platformCode
                    )
                );
                return false;
            }

            if (ownerJob
                && !AssignProcessToJobObject(
                    ownerJob,
                    processInformation.hProcess)) {
                const DWORD platformCode = GetLastError();
                TerminateProcess(processInformation.hProcess, 1);
                CloseHandle(processInformation.hThread);
                CloseHandle(processInformation.hProcess);
                CloseHandle(ownerJob);
                if (standardOutputRead) {
                    CloseHandle(standardOutputRead);
                }
                if (standardErrorRead) {
                    CloseHandle(standardErrorRead);
                }
                SetError(
                    error,
                    eSystemErrorCode::ProcessLaunchFailed,
                    platformCode,
                    "AssignProcessToJobObject failed.");
                return false;
            }
            if (ownerJob && ResumeThread(processInformation.hThread) == static_cast<DWORD>(-1)) {
                const DWORD platformCode = GetLastError();
                TerminateProcess(processInformation.hProcess, 1);
                CloseHandle(processInformation.hThread);
                CloseHandle(processInformation.hProcess);
                CloseHandle(ownerJob);
                if (standardOutputRead) {
                    CloseHandle(standardOutputRead);
                }
                if (standardErrorRead) {
                    CloseHandle(standardErrorRead);
                }
                SetError(
                    error,
                    eSystemErrorCode::ProcessLaunchFailed,
                    platformCode,
                    "ResumeThread failed.");
                return false;
            }
            CloseHandle(processInformation.hThread);
            _ProcessHandle = processInformation.hProcess;
            _OwnerJobHandle = ownerJob;
            _ProcessId = static_cast<UInt64>(processInformation.dwProcessId);
            _Running.store(true, std::memory_order_release);
            if (standardOutputRead) {
                _OutputThread = std::thread([this, standardOutputRead]() {
                    ReadPipe(standardOutputRead, false);
                });
            }
            if (standardErrorRead) {
                _ErrorThread = std::thread([this, standardErrorRead]() {
                    ReadPipe(standardErrorRead, true);
                });
            }
            _WaitThread = std::thread([this]() {
                WaitForSingleObject(_ProcessHandle, INFINITE);
                if (_OutputThread.joinable()) {
                    _OutputThread.join();
                }
                if (_ErrorThread.joinable()) {
                    _ErrorThread.join();
                }
                DWORD exitCode = static_cast<DWORD>(-1);
                GetExitCodeProcess(_ProcessHandle, &exitCode);
                _Running.store(false, std::memory_order_release);
                SystemProcessExitInfo exitInfo;
                exitInfo._ExitCode = static_cast<Int64>(exitCode);
                exitInfo._Reason = _TerminationRequested.load(std::memory_order_acquire)
                    ? eSystemProcessExitReason::Terminated
                    : eSystemProcessExitReason::Exited;
                const auto callback = _Callbacks._Exited;
                if (_Owner && callback) {
                    _Owner->PostCallback([callback, exitInfo]() {
                        callback(exitInfo);
                    });
                }
            });
            return true;
#else
            int standardOutputPipe[2]{-1, -1};
            int standardErrorPipe[2]{-1, -1};
            if (startInfo._CaptureOutput
                && (pipe(standardOutputPipe) != 0 || pipe(standardErrorPipe) != 0)) {
                const int platformCode = errno;
                if (standardOutputPipe[0] >= 0) {
                    close(standardOutputPipe[0]);
                }
                if (standardOutputPipe[1] >= 0) {
                    close(standardOutputPipe[1]);
                }
                if (standardErrorPipe[0] >= 0) {
                    close(standardErrorPipe[0]);
                }
                if (standardErrorPipe[1] >= 0) {
                    close(standardErrorPipe[1]);
                }
                SetError(error, eSystemErrorCode::ProcessIoFailed, platformCode, "Failed to create process output pipes.");
                return false;
            }

            std::vector<std::string> arguments;
            arguments.reserve(startInfo._Arguments.size() + 1);
            arguments.push_back(startInfo._Executable);
            arguments.insert(arguments.end(), startInfo._Arguments.begin(), startInfo._Arguments.end());
            std::vector<char*> argumentPointers;
            argumentPointers.reserve(arguments.size() + 1);
            for (std::string& argument : arguments) {
                argumentPointers.push_back(argument.data());
            }
            argumentPointers.push_back(nullptr);

            const pid_t processId = fork();
            if (processId < 0) {
                const int platformCode = errno;
                if (standardOutputPipe[0] >= 0) {
                    close(standardOutputPipe[0]);
                    close(standardOutputPipe[1]);
                    close(standardErrorPipe[0]);
                    close(standardErrorPipe[1]);
                }
                SetError(error, eSystemErrorCode::ProcessLaunchFailed, platformCode, "fork failed.");
                return false;
            }
            if (processId == 0) {
#if defined(__linux__)
                if (startInfo._TerminateWithOwner) {
                    const pid_t ownerProcessId = getppid();
                    if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0
                        || ownerProcessId <= 1
                        || getppid() != ownerProcessId) {
                        _exit(124);
                    }
                }
#endif
                if (!startInfo._WorkingDirectory.empty()
                    && chdir(startInfo._WorkingDirectory.c_str()) != 0) {
                    _exit(126);
                }
                if (startInfo._CaptureOutput) {
                    close(standardOutputPipe[0]);
                    close(standardErrorPipe[0]);
                    dup2(standardOutputPipe[1], STDOUT_FILENO);
                    dup2(standardErrorPipe[1], STDERR_FILENO);
                    close(standardOutputPipe[1]);
                    close(standardErrorPipe[1]);
                }
                for (const SystemProcessEnvironmentVariable& variable : startInfo._Environment) {
                    if (variable._Name.empty() || variable._Name.find('=') != std::string::npos) {
                        _exit(125);
                    }
                    setenv(variable._Name.c_str(), variable._Value.c_str(), 1);
                }
                execv(startInfo._Executable.c_str(), argumentPointers.data());
                _exit(127);
            }

            _NativeProcessId = processId;
            _ProcessId = static_cast<UInt64>(processId);
            _Running.store(true, std::memory_order_release);
            if (startInfo._CaptureOutput) {
                close(standardOutputPipe[1]);
                close(standardErrorPipe[1]);
                _OutputThread = std::thread([this, descriptor = standardOutputPipe[0]]() {
                    ReadPipe(descriptor, false);
                });
                _ErrorThread = std::thread([this, descriptor = standardErrorPipe[0]]() {
                    ReadPipe(descriptor, true);
                });
            }
            _WaitThread = std::thread([this]() {
                int status = 0;
                while (waitpid(_NativeProcessId, &status, 0) < 0 && errno == EINTR) {
                }
                if (_OutputThread.joinable()) {
                    _OutputThread.join();
                }
                if (_ErrorThread.joinable()) {
                    _ErrorThread.join();
                }
                _Running.store(false, std::memory_order_release);
                SystemProcessExitInfo exitInfo;
                exitInfo._ExitCode = WIFEXITED(status)
                    ? static_cast<Int64>(WEXITSTATUS(status))
                    : static_cast<Int64>(128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0));
                exitInfo._Reason = _TerminationRequested.load(std::memory_order_acquire)
                    ? eSystemProcessExitReason::Terminated
                    : eSystemProcessExitReason::Exited;
                const auto callback = _Callbacks._Exited;
                if (_Owner && callback) {
                    _Owner->PostCallback([callback, exitInfo]() {
                        callback(exitInfo);
                    });
                }
            });
            return true;
#endif
        }

        UInt64 GetProcessId() const override {
            return _ProcessId;
        }

        bool IsRunning() const override {
            return _Running.load(std::memory_order_acquire);
        }

        bool Terminate(SystemError* const error) override {
            if (error) {
                *error = {};
            }
            if (!IsRunning()) {
                SetError(error, eSystemErrorCode::InvalidArgument, 0, "The process is not running.");
                return false;
            }
            _TerminationRequested.store(true, std::memory_order_release);
#if defined(_WIN32)
            if (!TerminateProcess(_ProcessHandle, 1)) {
                SetError(error, eSystemErrorCode::PlatformFailure, GetLastError(), "TerminateProcess failed.");
                return false;
            }
            return true;
#else
            if (kill(_NativeProcessId, SIGTERM) != 0) {
                SetError(error, eSystemErrorCode::PlatformFailure, errno, "kill failed.");
                return false;
            }
            return true;
#endif
        }

        void Release() override {
            if (_Owner) {
                _Owner->ReleaseProcess(this);
            }
        }

        void Close() {
            if (IsRunning()) {
                SystemError ignoredError;
                Terminate(&ignoredError);
            }
            if (_WaitThread.joinable()) {
                _WaitThread.join();
            }
            if (_OutputThread.joinable()) {
                _OutputThread.join();
            }
            if (_ErrorThread.joinable()) {
                _ErrorThread.join();
            }
#if defined(_WIN32)
            if (_ProcessHandle) {
                CloseHandle(_ProcessHandle);
                _ProcessHandle = nullptr;
            }
            if (_OwnerJobHandle) {
                CloseHandle(_OwnerJobHandle);
                _OwnerJobHandle = nullptr;
            }
#endif
            _Owner = nullptr;
        }

    private:
#if defined(_WIN32)
        void ReadPipe(HANDLE pipe, const bool errorStream) {
            std::array<char, 4096> buffer{};
            DWORD readSize = 0;
            while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &readSize, nullptr)
                && readSize > 0) {
                const std::string content(buffer.data(), static_cast<std::size_t>(readSize));
                const auto callback = errorStream
                    ? _Callbacks._ErrorReceived
                    : _Callbacks._OutputReceived;
                if (_Owner && callback) {
                    _Owner->PostCallback([callback, content]() {
                        callback(content);
                    });
                }
            }
            CloseHandle(pipe);
        }

        HANDLE _ProcessHandle = nullptr;
        HANDLE _OwnerJobHandle = nullptr;
#else
        void ReadPipe(const int descriptor, const bool errorStream) {
            std::array<char, 4096> buffer{};
            while (true) {
                const ssize_t readSize = read(descriptor, buffer.data(), buffer.size());
                if (readSize <= 0) {
                    break;
                }
                const std::string content(buffer.data(), static_cast<std::size_t>(readSize));
                const auto callback = errorStream
                    ? _Callbacks._ErrorReceived
                    : _Callbacks._OutputReceived;
                if (_Owner && callback) {
                    _Owner->PostCallback([callback, content]() {
                        callback(content);
                    });
                }
            }
            close(descriptor);
        }

        pid_t _NativeProcessId = -1;
#endif
        System* _Owner = nullptr;
        SystemProcessCallbacks _Callbacks;
        UInt64 _ProcessId = 0;
        std::atomic_bool _Running = false;
        std::atomic_bool _TerminationRequested = false;
        std::thread _WaitThread;
        std::thread _OutputThread;
        std::thread _ErrorThread;
    };

    class SystemIpcServer;

    class SystemIpcConnection final : public ISystemIpcConnection {
    public:
        SystemIpcConnection(
            System* const owner,
            const SocketHandle socket,
            SystemIpcConnectionCallbacks callbacks,
            SystemIpcServer* const server
        )
            : _Owner(owner)
            , _Socket(socket)
            , _Callbacks(std::move(callbacks))
            , _Server(server) {
        }

        ~SystemIpcConnection() override {
            CloseInternal();
        }

        void Start();
        bool IsConnected() const override {
            return _Connected.load(std::memory_order_acquire);
        }

        bool Send(
            const void* const data,
            const std::size_t size,
            SystemError* const error
        ) override {
            if (error) {
                *error = {};
            }
            if (!IsConnected()) {
                SetError(error, eSystemErrorCode::IpcClosed, 0, "The IPC connection is closed.");
                return false;
            }
            if ((!data && size > 0) || size > SystemIpcMaximumMessageSize) {
                SetError(
                    error,
                    size > SystemIpcMaximumMessageSize
                        ? eSystemErrorCode::IpcMessageTooLarge
                        : eSystemErrorCode::InvalidArgument,
                    0,
                    size > SystemIpcMaximumMessageSize
                        ? "The IPC message exceeds the maximum size."
                        : "The IPC message data is null."
                );
                return false;
            }
            try {
                std::vector<UInt8> message(size);
                if (size > 0) {
                    std::memcpy(message.data(), data, size);
                }
                {
                    std::lock_guard<std::mutex> lock(_SendMutex);
                    if (!_Connected.load(std::memory_order_acquire)) {
                        SetError(error, eSystemErrorCode::IpcClosed, 0, "The IPC connection is closed.");
                        return false;
                    }
                    _SendQueue.emplace_back(std::move(message));
                }
                _SendCondition.notify_one();
                return true;
            }
            catch (...) {
                SetError(error, eSystemErrorCode::PlatformFailure, 0, "Failed to queue the IPC message.");
                return false;
            }
        }

        void Close() override {
            CloseInternal();
        }

        void Release() override {
            if (_Owner) {
                _Owner->ReleaseConnection(this);
            }
        }

        void CloseInternal() {
            const bool wasConnected = _Connected.exchange(false, std::memory_order_acq_rel);
            CloseSocket(_Socket);
            _SendCondition.notify_all();
            if (_ReadThread.joinable() && _ReadThread.get_id() != std::this_thread::get_id()) {
                _ReadThread.join();
            }
            if (_WriteThread.joinable() && _WriteThread.get_id() != std::this_thread::get_id()) {
                _WriteThread.join();
            }
            if (wasConnected) {
                PostDisconnected();
            }
        }

    private:
        void ReadLoop();
        void WriteLoop();
        void PostDisconnected();
        void PostError(eSystemErrorCode code, Int64 platformCode, const std::string& message);

        System* _Owner = nullptr;
        SocketHandle _Socket = InvalidSocketHandle;
        SystemIpcConnectionCallbacks _Callbacks;
        SystemIpcServer* _Server = nullptr;
        std::atomic_bool _Connected = false;
        std::atomic_bool _DisconnectPosted = false;
        std::thread _ReadThread;
        std::thread _WriteThread;
        std::mutex _SendMutex;
        std::condition_variable _SendCondition;
        std::deque<std::vector<UInt8>> _SendQueue;
    };

    class SystemIpcServer final : public ISystemIpcServer {
    public:
        SystemIpcServer(
            System* const owner,
            SystemIpcServerCallbacks callbacks
        )
            : _Owner(owner)
            , _Callbacks(std::move(callbacks)) {
        }

        ~SystemIpcServer() override {
            CloseInternal();
        }

        bool Start(SystemError* const error) {
            SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (socket == InvalidSocketHandle) {
                SetError(error, eSystemErrorCode::IpcListenFailed, LastSocketError(), "Failed to create the IPC listener socket.");
                return false;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
                || listen(socket, SOMAXCONN) != 0) {
                const Int64 platformCode = LastSocketError();
                CloseSocket(socket);
                SetError(error, eSystemErrorCode::IpcListenFailed, platformCode, "Failed to bind the local IPC listener.");
                return false;
            }

            sockaddr_in boundAddress{};
#if defined(_WIN32)
            int boundAddressSize = sizeof(boundAddress);
#else
            socklen_t boundAddressSize = sizeof(boundAddress);
#endif
            if (getsockname(socket, reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressSize) != 0) {
                const Int64 platformCode = LastSocketError();
                CloseSocket(socket);
                SetError(error, eSystemErrorCode::IpcListenFailed, platformCode, "Failed to read the local IPC endpoint.");
                return false;
            }

            _Socket = socket;
            _Endpoint._Address = "127.0.0.1:" + std::to_string(ntohs(boundAddress.sin_port));
            _Endpoint._AccessToken = CreateAccessToken();
            _Listening.store(true, std::memory_order_release);
            _AcceptThread = std::thread([this]() {
                AcceptLoop();
            });
            return true;
        }

        const SystemIpcEndpoint& GetEndpoint() const override {
            return _Endpoint;
        }

        void Close() override {
            CloseInternal();
        }

        void Release() override {
            if (_Owner) {
                _Owner->ReleaseServer(this);
            }
        }

        void CloseInternal() {
            _Listening.store(false, std::memory_order_release);
            if (_AcceptThread.joinable() && _AcceptThread.get_id() != std::this_thread::get_id()) {
                _AcceptThread.join();
            }
            CloseSocket(_Socket);
            std::vector<SystemIpcConnection*> connections;
            {
                std::lock_guard<std::mutex> lock(_ConnectionMutex);
                connections = _Connections;
                _Connections.clear();
            }
            for (SystemIpcConnection* const connection : connections) {
                if (connection) {
                    connection->CloseInternal();
                }
            }
        }

        void AddConnection(SystemIpcConnection* const connection) {
            std::lock_guard<std::mutex> lock(_ConnectionMutex);
            _Connections.push_back(connection);
        }

        void RemoveConnection(SystemIpcConnection* const connection) {
            std::lock_guard<std::mutex> lock(_ConnectionMutex);
            const auto iterator = std::find(_Connections.begin(), _Connections.end(), connection);
            if (iterator != _Connections.end()) {
                _Connections.erase(iterator);
            }
        }

        void PostMessage(SystemIpcConnection* const connection, const std::vector<UInt8>& data) {
            const auto callback = _Callbacks._MessageReceived;
            if (_Owner && callback) {
                _Owner->PostCallback([callback, connection, data]() {
                    callback(connection, data);
                });
            }
        }

        void PostClosed(SystemIpcConnection* const connection) {
            RemoveConnection(connection);
            const auto callback = _Callbacks._ConnectionClosed;
            if (_Owner && callback) {
                _Owner->PostCallback([callback, connection]() {
                    callback(connection);
                });
            }
        }

    private:
        void AcceptLoop() {
            while (_Listening.load(std::memory_order_acquire)) {
                fd_set readableSockets;
                FD_ZERO(&readableSockets);
                FD_SET(_Socket, &readableSockets);
                timeval timeout{};
                timeout.tv_usec = 50000;
#if defined(_WIN32)
                const int selected = select(
                    0,
                    &readableSockets,
                    nullptr,
                    nullptr,
                    &timeout
                );
#else
                const int selected = select(
                    _Socket + 1,
                    &readableSockets,
                    nullptr,
                    nullptr,
                    &timeout
                );
#endif
                if (!_Listening.load(std::memory_order_acquire)) {
                    return;
                }
                if (selected == 0) {
                    continue;
                }
                if (selected < 0) {
                    const auto callback = _Callbacks._ErrorOccurred;
                    const SystemError error{
                        eSystemErrorCode::IpcListenFailed,
                        LastSocketError(),
                        "The local IPC listener wait operation failed."
                    };
                    if (_Owner && callback) {
                        _Owner->PostCallback([callback, error]() {
                            callback(error);
                        });
                    }
                    return;
                }

                sockaddr_in clientAddress{};
#if defined(_WIN32)
                int clientAddressSize = sizeof(clientAddress);
#else
                socklen_t clientAddressSize = sizeof(clientAddress);
#endif
                SocketHandle clientSocket = accept(
                    _Socket,
                    reinterpret_cast<sockaddr*>(&clientAddress),
                    &clientAddressSize
                );
                if (clientSocket == InvalidSocketHandle) {
                    if (_Listening.load(std::memory_order_acquire)) {
                        const auto callback = _Callbacks._ErrorOccurred;
                        const SystemError error{
                            eSystemErrorCode::IpcListenFailed,
                            LastSocketError(),
                            "The local IPC accept operation failed."
                        };
                        if (_Owner && callback) {
                            _Owner->PostCallback([callback, error]() {
                                callback(error);
                            });
                        }
                    }
                    return;
                }

                if (!SetSocketReceiveTimeout(clientSocket, 5000)) {
                    CloseSocket(clientSocket);
                    continue;
                }
                std::vector<UInt8> tokenData;
                if (!ReceiveFrame(clientSocket, tokenData)
                    || std::string(tokenData.begin(), tokenData.end()) != _Endpoint._AccessToken) {
                    CloseSocket(clientSocket);
                    continue;
                }
                SetSocketReceiveTimeout(clientSocket, 0);
                static constexpr char s_AuthenticationAccepted[] = "OK";
                if (!SendFrame(
                    clientSocket,
                    s_AuthenticationAccepted,
                    sizeof(s_AuthenticationAccepted) - 1
                )) {
                    CloseSocket(clientSocket);
                    continue;
                }

                SystemIpcConnection* connection = nullptr;
                try {
                    connection = new SystemIpcConnection(_Owner, clientSocket, {}, this);
                }
                catch (...) {
                    CloseSocket(clientSocket);
                    continue;
                }
                _Owner->RegisterConnection(connection);
                AddConnection(connection);
                connection->Start();
                const auto callback = _Callbacks._ConnectionAccepted;
                if (_Owner && callback) {
                    _Owner->PostCallback([callback, connection]() {
                        callback(connection);
                    });
                }
            }
        }

        System* _Owner = nullptr;
        SystemIpcServerCallbacks _Callbacks;
        SystemIpcEndpoint _Endpoint;
        SocketHandle _Socket = InvalidSocketHandle;
        std::atomic_bool _Listening = false;
        std::thread _AcceptThread;
        std::mutex _ConnectionMutex;
        std::vector<SystemIpcConnection*> _Connections;
    };

    void SystemIpcConnection::Start() {
        _Connected.store(true, std::memory_order_release);
        _ReadThread = std::thread([this]() {
            ReadLoop();
        });
        _WriteThread = std::thread([this]() {
            WriteLoop();
        });
        const auto callback = _Callbacks._Connected;
        if (_Owner && callback) {
            _Owner->PostCallback([callback]() {
                callback();
            });
        }
    }

    void SystemIpcConnection::ReadLoop() {
        while (_Connected.load(std::memory_order_acquire)) {
            std::vector<UInt8> data;
            if (!ReceiveFrame(_Socket, data)) {
                break;
            }
            if (_Server) {
                _Server->PostMessage(this, data);
            }
            else {
                const auto callback = _Callbacks._MessageReceived;
                if (_Owner && callback) {
                    _Owner->PostCallback([callback, data]() {
                        callback(data);
                    });
                }
            }
        }
        const bool wasConnected = _Connected.exchange(false, std::memory_order_acq_rel);
        CloseSocket(_Socket);
        _SendCondition.notify_all();
        if (wasConnected) {
            PostDisconnected();
        }
    }

    void SystemIpcConnection::WriteLoop() {
        while (true) {
            std::vector<UInt8> data;
            {
                std::unique_lock<std::mutex> lock(_SendMutex);
                _SendCondition.wait(lock, [this]() {
                    return !_Connected.load(std::memory_order_acquire) || !_SendQueue.empty();
                });
                if (_SendQueue.empty()) {
                    return;
                }
                data = std::move(_SendQueue.front());
                _SendQueue.pop_front();
            }
            if (!SendFrame(_Socket, data.data(), data.size())) {
                PostError(eSystemErrorCode::IpcClosed, LastSocketError(), "Failed to send an IPC message.");
                const bool wasConnected = _Connected.exchange(false, std::memory_order_acq_rel);
                CloseSocket(_Socket);
                _SendCondition.notify_all();
                if (wasConnected) {
                    PostDisconnected();
                }
                return;
            }
        }
    }

    void SystemIpcConnection::PostDisconnected() {
        if (_DisconnectPosted.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (_Server) {
            _Server->PostClosed(this);
            return;
        }
        const auto callback = _Callbacks._Disconnected;
        if (_Owner && callback) {
            _Owner->PostCallback([callback]() {
                callback();
            });
        }
    }

    void SystemIpcConnection::PostError(
        const eSystemErrorCode code,
        const Int64 platformCode,
        const std::string& message
    ) {
        const auto callback = _Callbacks._ErrorOccurred;
        if (!_Owner || !callback) {
            return;
        }
        const SystemError error{code, platformCode, message};
        _Owner->PostCallback([callback, error]() {
            callback(error);
        });
    }

    System::~System() {
        delete _FileSystemWatchers; // Stop the native producer before destroying its state.
    }

    bool System::Initialize(FoundationApi* const foundation) {
        if (!foundation || _Foundation) {
            return false;
        }
#if defined(_WIN32)
        WSADATA socketData{};
        if (WSAStartup(MAKEWORD(2, 2), &socketData) != 0) {
            return false;
        }
#endif
        _Foundation = foundation;
        _Launched = false;
        return true;
    }

    bool System::Launch(FoundationApi* const foundation) {
        if (foundation != _Foundation || _Launched) {
            return false;
        }
        _Launched = true;
        return true;
    }

    void System::Release(FoundationApi* const foundation) {
        if (foundation != _Foundation) {
            return;
        }
        _Launched = false;
        if (_FileSystemWatchers) {
            _FileSystemWatchers->Stop();
        }

        // Complete already queued notifications and deferred object destruction
        // while their callback arguments are still alive.
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

        std::vector<SystemProcess*> processes;
        std::vector<SystemIpcConnection*> connections;
        std::vector<SystemIpcServer*> servers;
        {
            std::lock_guard<std::mutex> lock(_ObjectMutex);
            processes.assign(_Processes.begin(), _Processes.end());
            connections.assign(_Connections.begin(), _Connections.end());
            servers.assign(_Servers.begin(), _Servers.end());
            _Processes.clear();
            _Connections.clear();
            _Servers.clear();
        }
        for (SystemIpcServer* const server : servers) {
            delete server;
        }
        for (SystemIpcConnection* const connection : connections) {
            delete connection;
        }
        for (SystemProcess* const process : processes) {
            delete process;
        }
        {
            std::lock_guard<std::mutex> lock(_CallbackMutex);
            _Callbacks.clear();
        }
        _Foundation = nullptr;
#if defined(_WIN32)
        WSACleanup();
#endif
    }

    void System::EarlyUpdate(FoundationApi* const foundation) {
        if (_FileSystemWatchers) {
            _FileSystemWatchers->Dispatch();
        }
    }

    void System::Update(FoundationApi* const foundation) {
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

    void System::LaterUpdate(FoundationApi* const foundation) {
        (void)foundation;
    }

    ISystemProcess* System::StartProcess(
        const SystemProcessStartInfo& startInfo,
        const SystemProcessCallbacks& callbacks,
        SystemError* const error
    ) {
        if (error) {
            *error = {};
        }
        if (!_Launched) {
            SetError(error, eSystemErrorCode::SystemUnavailable, 0, "The system component is not running.");
            return nullptr;
        }
        if (startInfo._Executable.empty()) {
            SetError(error, eSystemErrorCode::InvalidArgument, 0, "The process executable is empty.");
            return nullptr;
        }
#if !defined(_WIN32)
        if (access(startInfo._Executable.c_str(), X_OK) != 0) {
            const int platformCode = errno;
            SetError(
                error,
                eSystemErrorCode::ProcessLaunchFailed,
                platformCode,
                BuildPosixProcessError(
                    "The process executable is unavailable.",
                    startInfo,
                    platformCode
                )
            );
            return nullptr;
        }
        if (!startInfo._WorkingDirectory.empty()
            && access(startInfo._WorkingDirectory.c_str(), X_OK) != 0) {
            const int platformCode = errno;
            SetError(
                error,
                eSystemErrorCode::ProcessLaunchFailed,
                platformCode,
                BuildPosixProcessError(
                    "The process working directory is unavailable.",
                    startInfo,
                    platformCode
                )
            );
            return nullptr;
        }
#endif
        SystemProcess* process = nullptr;
        try {
            process = new SystemProcess(this, callbacks);
            if (!process->Start(startInfo, error)) {
                delete process;
                return nullptr;
            }
            std::lock_guard<std::mutex> lock(_ObjectMutex);
            _Processes.insert(process);
            return process;
        }
        catch (...) {
            delete process;
            SetError(error, eSystemErrorCode::ProcessLaunchFailed, 0, "Failed to allocate the process object.");
            return nullptr;
        }
    }

    ISystemIpcServer* System::CreateLocalIpcServer(
        const SystemIpcServerCallbacks& callbacks,
        SystemError* const error
    ) {
        if (error) {
            *error = {};
        }
        if (!_Launched) {
            SetError(error, eSystemErrorCode::SystemUnavailable, 0, "The system component is not running.");
            return nullptr;
        }
        SystemIpcServer* server = nullptr;
        try {
            server = new SystemIpcServer(this, callbacks);
            if (!server->Start(error)) {
                delete server;
                return nullptr;
            }
            std::lock_guard<std::mutex> lock(_ObjectMutex);
            _Servers.insert(server);
            return server;
        }
        catch (...) {
            delete server;
            SetError(error, eSystemErrorCode::IpcListenFailed, 0, "Failed to allocate the IPC server.");
            return nullptr;
        }
    }

    ISystemIpcConnection* System::ConnectLocalIpc(
        const SystemIpcEndpoint& endpoint,
        const SystemIpcConnectionCallbacks& callbacks,
        SystemError* const error
    ) {
        if (error) {
            *error = {};
        }
        if (!_Launched) {
            SetError(error, eSystemErrorCode::SystemUnavailable, 0, "The system component is not running.");
            return nullptr;
        }
        const std::size_t separator = endpoint._Address.rfind(':');
        if (!endpoint.IsValid() || separator == std::string::npos) {
            SetError(error, eSystemErrorCode::InvalidArgument, 0, "The IPC endpoint is invalid.");
            return nullptr;
        }
        const std::string host = endpoint._Address.substr(0, separator);
        const std::string portText = endpoint._Address.substr(separator + 1);
        int port = 0;
        try {
            port = std::stoi(portText);
        }
        catch (...) {
            SetError(error, eSystemErrorCode::InvalidArgument, 0, "The IPC endpoint port is invalid.");
            return nullptr;
        }
        if (host != "127.0.0.1" || port <= 0 || port > 65535) {
            SetError(error, eSystemErrorCode::InvalidArgument, 0, "Only local loopback IPC endpoints are accepted.");
            return nullptr;
        }

        SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == InvalidSocketHandle) {
            SetError(error, eSystemErrorCode::IpcConnectFailed, LastSocketError(), "Failed to create the IPC client socket.");
            return nullptr;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<UInt16>(port));
        inet_pton(AF_INET, host.c_str(), &address.sin_addr);
        if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0
            || !SendFrame(socket, endpoint._AccessToken.data(), endpoint._AccessToken.size())) {
            const Int64 platformCode = LastSocketError();
            CloseSocket(socket);
            SetError(error, eSystemErrorCode::IpcConnectFailed, platformCode, "Failed to connect to the local IPC endpoint.");
            return nullptr;
        }
        if (!SetSocketReceiveTimeout(socket, 5000)) {
            const Int64 platformCode = LastSocketError();
            CloseSocket(socket);
            SetError(error, eSystemErrorCode::IpcConnectFailed, platformCode, "Failed to configure the local IPC authentication timeout.");
            return nullptr;
        }
        std::vector<UInt8> authenticationResponse;
        if (!ReceiveFrame(socket, authenticationResponse)
            || std::string(authenticationResponse.begin(), authenticationResponse.end()) != "OK") {
            const Int64 platformCode = LastSocketError();
            CloseSocket(socket);
            SetError(error, eSystemErrorCode::IpcAuthenticationFailed, platformCode, "The local IPC endpoint rejected authentication.");
            return nullptr;
        }
        SetSocketReceiveTimeout(socket, 0);

        SystemIpcConnection* connection = nullptr;
        try {
            connection = new SystemIpcConnection(this, socket, callbacks, nullptr);
            RegisterConnection(connection);
            connection->Start();
            return connection;
        }
        catch (...) {
            delete connection;
            CloseSocket(socket);
            SetError(error, eSystemErrorCode::IpcConnectFailed, 0, "Failed to allocate the IPC connection.");
            return nullptr;
        }
    }


    bool System::FileSystemWatcher(const std::string & path, void * const context, fOnWatcherCallback callback) {
        if (!_Foundation || !callback) {
            return false;
        }
        try {
            if (!_FileSystemWatchers) {
                _FileSystemWatchers = new SystemFileWatchers();
            }
            std::string error;
            if (_FileSystemWatchers->Add(path, context, std::move(callback), error)) {
                return true;
            }
            std::fprintf(stderr, "[Foundation][System] Cannot watch '%s': %s\n",
                         path.c_str(), error.c_str());
        } catch (...) {
            std::fputs("[Foundation][System] Failed to register filesystem watcher.\n", stderr);
        }
        return false;
    }

    void System::PostCallback(std::function<void()> callback) {
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

    void System::RegisterConnection(SystemIpcConnection* const connection) {
        if (!connection) {
            return;
        }
        std::lock_guard<std::mutex> lock(_ObjectMutex);
        _Connections.insert(connection);
    }

    void System::ReleaseProcess(SystemProcess* const process) {
        if (!process) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_ObjectMutex);
            _Processes.erase(process);
        }
        delete process;
    }

    void System::ReleaseServer(SystemIpcServer* const server) {
        if (!server) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_ObjectMutex);
            _Servers.erase(server);
        }
        delete server;
    }

    void System::ReleaseConnection(SystemIpcConnection* const connection) {
        if (!connection) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(_ObjectMutex);
            _Connections.erase(connection);
        }
        connection->CloseInternal();
        PostCallback([connection]() {
            delete connection;
        });
    }
}

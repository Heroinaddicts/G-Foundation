#ifndef __XDumper_h__
#define __XDumper_h__

#include "MultiSys.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>
#endif // defined(_WIN32)

namespace FoundationUtils {
    class XDumper {
    public:
        static XDumper& GetInstance();
        ~XDumper();

        // Compatibility entry point. When FOUNDATION_CRASH_ARTIFACT_DIR is set, the
        // supplied path contributes the file name while the monitor-owned per-run
        // artifact directory remains authoritative.
        void SetDumpFilename(const char* dumpName);

    private:
        XDumper();
        XDumper(const XDumper& rhs) = delete;
        XDumper& operator=(const XDumper& rhs) = delete;

        static void EnsureDumpDirectory();

#if defined(_WIN32)
        static void CreateMinidump(EXCEPTION_POINTERS* exceptionPointers);
        static long __stdcall SnUnhandledExceptionFilter(PEXCEPTION_POINTERS exceptionInfo);

        LPTOP_LEVEL_EXCEPTION_FILTER _PreviousFilter;
#endif // defined(_WIN32)

#if !defined(_WIN32)
        static void EnableCoreDump();
        static void PreparePosixCrashContext();
#endif // !defined(_WIN32)
    };
}

#if defined(FOUNDATION_UTILS_XDUMPER_BUILD)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <Psapi.h>
#include <Strsafe.h>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")
#endif // defined(_WIN32)

#if !defined(_WIN32)
#include <sys/resource.h>
#include <unistd.h>
#endif // !defined(_WIN32)

#if defined(__linux__)
#include <sys/prctl.h>
#endif // defined(__linux__)

namespace {
    constexpr std::size_t s_PathCapacity = 32768U;
    constexpr std::size_t s_EnvironmentValueCapacity = 4096U;

#if !defined(_WIN32)
    char s_DumpName[s_PathCapacity] = {};
    char s_ArtifactDirectoryName[s_PathCapacity] = {};

    const char* ReadFirstEnvironmentValue(
        const char* primaryName,
        const char* compatibilityName = nullptr
    ) {
        const char* value = std::getenv(primaryName);
        if (value != nullptr && value[0] != '\0') {
            return value;
        }

        if (compatibilityName != nullptr) {
            value = std::getenv(compatibilityName);
            if (value != nullptr && value[0] != '\0') {
                return value;
            }
        }
        return nullptr;
    }

    std::string SanitizePathSegment(const char* value, const char* fallback) {
        if (value == nullptr || value[0] == '\0') {
            return fallback;
        }

        std::string result;
        result.reserve(96U);
        std::uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char character : std::string(value)) {
            hash ^= character;
            hash *= 1099511628211ULL;

            if (result.size() < 80U) {
                const bool isAsciiLetter =
                    (character >= static_cast<unsigned char>('a') && character <= static_cast<unsigned char>('z')) ||
                    (character >= static_cast<unsigned char>('A') && character <= static_cast<unsigned char>('Z'));
                const bool isDigit = character >= static_cast<unsigned char>('0') && character <= static_cast<unsigned char>('9');
                const bool isSafePunctuation = character == '-' || character == '_' || character == '.';
                result.push_back(isAsciiLetter || isDigit || isSafePunctuation ? static_cast<char>(character) : '_');
            }
        }

        char hashSuffix[24] = {};
        std::snprintf(hashSuffix, sizeof(hashSuffix), "_%016llx", static_cast<unsigned long long>(hash));
        result.append(hashSuffix);
        return result;
    }

    std::filesystem::path BuildPerRunArtifactDirectory() {
        const char* artifactRoot = ReadFirstEnvironmentValue("FOUNDATION_CRASH_ARTIFACT_DIR");
        if (artifactRoot == nullptr) {
            artifactRoot = "Dump";
        }

        const char* runId = ReadFirstEnvironmentValue("FOUNDATION_RUN_ID", "FOUNDATION_MONITOR_RUN_ID");
        const char* generation = ReadFirstEnvironmentValue("FOUNDATION_RUN_GENERATION", "FOUNDATION_MONITOR_GENERATION");
        if (generation == nullptr) {
            generation = ReadFirstEnvironmentValue("FOUNDATION_GENERATION");
        }

        return std::filesystem::u8path(artifactRoot) /
            SanitizePathSegment(runId, "unmanaged") /
            ("generation-" + SanitizePathSegment(generation, "unknown"));
    }

    bool CopyPathToBuffer(const std::filesystem::path& path, char* destination, const std::size_t destinationSize) {
        const std::string pathUtf8 = path.string();
        if (pathUtf8.size() >= destinationSize) {
            return false;
        }
        std::memcpy(destination, pathUtf8.c_str(), pathUtf8.size() + 1U);
        return true;
    }
#endif // !defined(_WIN32)

#if defined(_WIN32)
    wchar_t s_DumpNameWindows[s_PathCapacity] = {};
    wchar_t s_RunArtifactDirectoryWindows[s_PathCapacity] = {};
    wchar_t s_ActualDumpPathWindows[s_PathCapacity] = {};
    wchar_t s_MetadataPathWindows[s_PathCapacity] = {};
    wchar_t s_RunIdWindows[s_EnvironmentValueCapacity] = L"unmanaged";
    wchar_t s_GenerationWindows[s_EnvironmentValueCapacity] = L"unknown";
    char s_MetadataUtf8Buffer[s_EnvironmentValueCapacity * 3U] = {};
    bool s_HasConfiguredArtifactRoot = false;
    volatile LONG s_DumpInProgress = 0;
    volatile LONG s_CollisionSequence = 0;

    bool ConvertUtf8ToWide(const char* source, wchar_t* destination, const std::size_t destinationCapacity) {
        if (source == nullptr || destination == nullptr || destinationCapacity == 0U) {
            return false;
        }

        int requiredLength = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, nullptr, 0);
        UINT codePage = CP_UTF8;
        DWORD flags = MB_ERR_INVALID_CHARS;
        if (requiredLength <= 0) {
            // Preserve callers of the historical ANSI API while all filesystem
            // operations after this conversion remain UTF-16.
            codePage = CP_ACP;
            flags = 0;
            requiredLength = ::MultiByteToWideChar(codePage, flags, source, -1, nullptr, 0);
        }
        if (requiredLength <= 0 || static_cast<std::size_t>(requiredLength) > destinationCapacity) {
            return false;
        }
        return ::MultiByteToWideChar(
            codePage,
            flags,
            source,
            -1,
            destination,
            static_cast<int>(destinationCapacity)
        ) > 0;
    }

    bool ReadEnvironmentWide(const wchar_t* name, wchar_t* destination, const DWORD destinationCapacity) {
        const DWORD length = ::GetEnvironmentVariableW(name, destination, destinationCapacity);
        if (length == 0U || length >= destinationCapacity) {
            destination[0] = L'\0';
            return false;
        }
        return true;
    }

    bool ReadFirstEnvironmentWide(
        const wchar_t* primaryName,
        const wchar_t* compatibilityName,
        wchar_t* destination,
        const DWORD destinationCapacity
    ) {
        if (ReadEnvironmentWide(primaryName, destination, destinationCapacity)) {
            return true;
        }
        return compatibilityName != nullptr &&
            ReadEnvironmentWide(compatibilityName, destination, destinationCapacity);
    }

    std::uint64_t HashWideString(const wchar_t* value) {
        std::uint64_t hash = 14695981039346656037ULL;
        while (value != nullptr && *value != L'\0') {
            const std::uint16_t codeUnit = static_cast<std::uint16_t>(*value++);
            hash ^= static_cast<std::uint8_t>(codeUnit & 0xFFU);
            hash *= 1099511628211ULL;
            hash ^= static_cast<std::uint8_t>(codeUnit >> 8U);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    bool IsSafeAsciiPathCharacter(const wchar_t character) {
        return (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z') ||
            (character >= L'0' && character <= L'9') ||
            character == L'-' || character == L'_' || character == L'.';
    }

    void SanitizeWidePathSegment(
        const wchar_t* source,
        const wchar_t* fallback,
        wchar_t* destination,
        const std::size_t destinationCapacity
    ) {
        if (source == nullptr || source[0] == L'\0') {
            (void)::StringCchCopyW(destination, destinationCapacity, fallback);
            return;
        }

        const std::uint64_t hash = HashWideString(source);
        std::size_t outputLength = 0U;
        while (*source != L'\0' && outputLength < 80U && outputLength + 20U < destinationCapacity) {
            destination[outputLength++] = IsSafeAsciiPathCharacter(*source) ? *source : L'_';
            ++source;
        }
        destination[outputLength] = L'\0';

        wchar_t hashSuffix[24] = {};
        (void)::StringCchPrintfW(hashSuffix, std::size(hashSuffix), L"_%016llx", hash);
        (void)::StringCchCatW(destination, destinationCapacity, hashSuffix);
    }

    bool AppendPathComponent(
        wchar_t* path,
        const std::size_t pathCapacity,
        const wchar_t* component
    ) {
        const std::size_t length = wcslen(path);
        if (length > 0U && path[length - 1U] != L'\\' && path[length - 1U] != L'/') {
            if (FAILED(::StringCchCatW(path, pathCapacity, L"\\"))) {
                return false;
            }
        }
        return SUCCEEDED(::StringCchCatW(path, pathCapacity, component));
    }

    void InitializeWindowsArtifactPaths() {
        wchar_t artifactRoot[s_PathCapacity] = L"Dump";
        wchar_t environmentValue[s_PathCapacity] = {};
        if (ReadEnvironmentWide(L"FOUNDATION_CRASH_ARTIFACT_DIR", environmentValue, static_cast<DWORD>(std::size(environmentValue)))) {
            (void)::StringCchCopyW(artifactRoot, std::size(artifactRoot), environmentValue);
            s_HasConfiguredArtifactRoot = true;
        }

        if (!ReadFirstEnvironmentWide(
            L"FOUNDATION_RUN_ID",
            L"FOUNDATION_MONITOR_RUN_ID",
            s_RunIdWindows,
            static_cast<DWORD>(std::size(s_RunIdWindows))
        )) {
            (void)::StringCchCopyW(s_RunIdWindows, std::size(s_RunIdWindows), L"unmanaged");
        }
        if (!ReadFirstEnvironmentWide(
            L"FOUNDATION_RUN_GENERATION",
            L"FOUNDATION_MONITOR_GENERATION",
            s_GenerationWindows,
            static_cast<DWORD>(std::size(s_GenerationWindows))
        )) {
            if (!ReadEnvironmentWide(
                L"FOUNDATION_GENERATION",
                s_GenerationWindows,
                static_cast<DWORD>(std::size(s_GenerationWindows))
            )) {
                (void)::StringCchCopyW(s_GenerationWindows, std::size(s_GenerationWindows), L"unknown");
            }
        }

        wchar_t runSegment[128] = {};
        wchar_t generationSegment[160] = L"generation-";
        wchar_t sanitizedGeneration[128] = {};
        SanitizeWidePathSegment(s_RunIdWindows, L"unmanaged", runSegment, std::size(runSegment));
        SanitizeWidePathSegment(s_GenerationWindows, L"unknown", sanitizedGeneration, std::size(sanitizedGeneration));
        (void)::StringCchCatW(generationSegment, std::size(generationSegment), sanitizedGeneration);

        (void)::StringCchCopyW(s_RunArtifactDirectoryWindows, std::size(s_RunArtifactDirectoryWindows), artifactRoot);
        if (!AppendPathComponent(s_RunArtifactDirectoryWindows, std::size(s_RunArtifactDirectoryWindows), runSegment) ||
            !AppendPathComponent(s_RunArtifactDirectoryWindows, std::size(s_RunArtifactDirectoryWindows), generationSegment)) {
            (void)::StringCchCopyW(s_RunArtifactDirectoryWindows, std::size(s_RunArtifactDirectoryWindows), L"Dump\\unmanaged\\generation-unknown");
        }

        FILETIME fileTime = {};
        ::GetSystemTimeAsFileTime(&fileTime);
        ULARGE_INTEGER timestamp = {};
        timestamp.LowPart = fileTime.dwLowDateTime;
        timestamp.HighPart = fileTime.dwHighDateTime;

        wchar_t fileName[160] = {};
        (void)::StringCchPrintfW(
            fileName,
            std::size(fileName),
            L"FoundationEngine_%lu_%llu.dmp",
            ::GetCurrentProcessId(),
            timestamp.QuadPart
        );
        (void)::StringCchCopyW(s_DumpNameWindows, std::size(s_DumpNameWindows), s_RunArtifactDirectoryWindows);
        (void)AppendPathComponent(s_DumpNameWindows, std::size(s_DumpNameWindows), fileName);
    }

    const wchar_t* FindFileName(const wchar_t* path) {
        const wchar_t* fileName = path;
        for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
            if (*cursor == L'\\' || *cursor == L'/') {
                fileName = cursor + 1;
            }
        }
        return fileName;
    }

    bool BuildCollisionPath(
        const wchar_t* basePath,
        wchar_t* collisionPath,
        const std::size_t collisionPathCapacity
    ) {
        const wchar_t* fileName = FindFileName(basePath);
        const wchar_t* extension = nullptr;
        for (const wchar_t* cursor = fileName; *cursor != L'\0'; ++cursor) {
            if (*cursor == L'.') {
                extension = cursor;
            }
        }

        const std::size_t stemLength = extension == nullptr ? wcslen(basePath) : static_cast<std::size_t>(extension - basePath);
        if (FAILED(::StringCchCopyNW(collisionPath, collisionPathCapacity, basePath, stemLength))) {
            return false;
        }

        FILETIME fileTime = {};
        ::GetSystemTimeAsFileTime(&fileTime);
        ULARGE_INTEGER timestamp = {};
        timestamp.LowPart = fileTime.dwLowDateTime;
        timestamp.HighPart = fileTime.dwHighDateTime;

        wchar_t suffix[160] = {};
        const LONG sequence = ::InterlockedIncrement(&s_CollisionSequence);
        if (FAILED(::StringCchPrintfW(
            suffix,
            std::size(suffix),
            L"_%lu_%lu_%llu_%ld%s",
            ::GetCurrentProcessId(),
            ::GetCurrentThreadId(),
            timestamp.QuadPart,
            sequence,
            extension == nullptr ? L".dmp" : extension
        ))) {
            return false;
        }
        return SUCCEEDED(::StringCchCatW(collisionPath, collisionPathCapacity, suffix));
    }

    HANDLE CreateUniqueDumpFile(wchar_t* actualPath, const std::size_t actualPathCapacity, DWORD* openError) {
        (void)::StringCchCopyW(actualPath, actualPathCapacity, s_DumpNameWindows);
        for (int attempt = 0; attempt < 16; ++attempt) {
            HANDLE file = ::CreateFileW(
                actualPath,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );
            if (file != INVALID_HANDLE_VALUE) {
                *openError = ERROR_SUCCESS;
                return file;
            }

            *openError = ::GetLastError();
            if (*openError != ERROR_FILE_EXISTS && *openError != ERROR_ALREADY_EXISTS) {
                break;
            }
            if (!BuildCollisionPath(s_DumpNameWindows, actualPath, actualPathCapacity)) {
                *openError = ERROR_INSUFFICIENT_BUFFER;
                break;
            }
        }
        return INVALID_HANDLE_VALUE;
    }

    bool BuildMetadataPath(const wchar_t* dumpPath, wchar_t* metadataPath, const std::size_t metadataPathCapacity) {
        const wchar_t* fileName = FindFileName(dumpPath);
        const wchar_t* extension = nullptr;
        for (const wchar_t* cursor = fileName; *cursor != L'\0'; ++cursor) {
            if (*cursor == L'.') {
                extension = cursor;
            }
        }

        const std::size_t stemLength = extension == nullptr ? wcslen(dumpPath) : static_cast<std::size_t>(extension - dumpPath);
        if (FAILED(::StringCchCopyNW(metadataPath, metadataPathCapacity, dumpPath, stemLength))) {
            return false;
        }
        return SUCCEEDED(::StringCchCatW(metadataPath, metadataPathCapacity, L".metadata.txt"));
    }

    bool WriteBytes(const HANDLE file, const char* data, const DWORD byteCount) {
        DWORD written = 0U;
        return ::WriteFile(file, data, byteCount, &written, nullptr) != FALSE && written == byteCount;
    }

    bool WriteLiteral(const HANDLE file, const char* value) {
        DWORD length = 0U;
        while (value[length] != '\0') {
            ++length;
        }
        return WriteBytes(file, value, length);
    }

    DWORD FormatUnsignedDecimal(char* destination, const DWORD destinationCapacity, std::uint64_t value) {
        char reversed[32] = {};
        DWORD digitCount = 0U;
        do {
            reversed[digitCount++] = static_cast<char>('0' + (value % 10U));
            value /= 10U;
        } while (value != 0U && digitCount < static_cast<DWORD>(std::size(reversed)));

        if (digitCount + 1U > destinationCapacity) {
            return 0U;
        }
        for (DWORD index = 0U; index < digitCount; ++index) {
            destination[index] = reversed[digitCount - index - 1U];
        }
        destination[digitCount] = '\0';
        return digitCount;
    }

    DWORD FormatUnsignedHex(char* destination, const DWORD destinationCapacity, std::uint64_t value) {
        static constexpr char s_HexDigits[] = "0123456789abcdef";
        char reversed[32] = {};
        DWORD digitCount = 0U;
        do {
            reversed[digitCount++] = s_HexDigits[value & 0xFU];
            value >>= 4U;
        } while (value != 0U && digitCount < static_cast<DWORD>(std::size(reversed)));

        if (digitCount + 3U > destinationCapacity) {
            return 0U;
        }
        destination[0] = '0';
        destination[1] = 'x';
        for (DWORD index = 0U; index < digitCount; ++index) {
            destination[index + 2U] = reversed[digitCount - index - 1U];
        }
        destination[digitCount + 2U] = '\0';
        return digitCount + 2U;
    }

    void WriteUnsignedLine(const HANDLE file, const char* name, const std::uint64_t value, const bool hexadecimal = false) {
        char number[40] = {};
        const DWORD length = hexadecimal ?
            FormatUnsignedHex(number, static_cast<DWORD>(std::size(number)), value) :
            FormatUnsignedDecimal(number, static_cast<DWORD>(std::size(number)), value);
        (void)WriteLiteral(file, name);
        (void)WriteBytes(file, number, length);
        (void)WriteLiteral(file, "\r\n");
    }

    void WriteWideUtf8Line(const HANDLE file, const char* name, const wchar_t* value) {
        const int byteCount = ::WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            s_MetadataUtf8Buffer,
            static_cast<int>(std::size(s_MetadataUtf8Buffer)),
            nullptr,
            nullptr
        );
        (void)WriteLiteral(file, name);
        if (byteCount > 0) {
            (void)WriteBytes(file, s_MetadataUtf8Buffer, static_cast<DWORD>(byteCount - 1));
        }
        (void)WriteLiteral(file, "\r\n");
    }

    void WriteWindowsMetadata(
        const wchar_t* dumpPath,
        const EXCEPTION_POINTERS* exceptionPointers,
        const BOOL dumpWritten,
        const DWORD dumpError,
        const std::uint64_t dumpBytes,
        const std::uint64_t timestamp
    ) {
        s_MetadataPathWindows[0] = L'\0';
        if (!BuildMetadataPath(dumpPath, s_MetadataPathWindows, std::size(s_MetadataPathWindows))) {
            return;
        }

        HANDLE metadataFile = ::CreateFileW(
            s_MetadataPathWindows,
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (metadataFile == INVALID_HANDLE_VALUE) {
            return;
        }

        PROCESS_MEMORY_COUNTERS_EX processMemory = {};
        processMemory.cb = sizeof(processMemory);
        const BOOL hasProcessMemory = ::GetProcessMemoryInfo(
            ::GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&processMemory),
            sizeof(processMemory)
        );

        MEMORYSTATUSEX systemMemory = {};
        systemMemory.dwLength = sizeof(systemMemory);
        const BOOL hasSystemMemory = ::GlobalMemoryStatusEx(&systemMemory);

        DWORD exceptionCode = 0U;
        std::uint64_t exceptionAddress = 0U;
        if (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr) {
            exceptionCode = exceptionPointers->ExceptionRecord->ExceptionCode;
            exceptionAddress = reinterpret_cast<std::uintptr_t>(exceptionPointers->ExceptionRecord->ExceptionAddress);
        }

        (void)WriteLiteral(metadataFile, "format_version=1\r\nartifact_kind=windows_minidump\r\n");
        WriteWideUtf8Line(metadataFile, "run_id=", s_RunIdWindows);
        WriteWideUtf8Line(metadataFile, "generation=", s_GenerationWindows);
        WriteUnsignedLine(metadataFile, "utc_file_time_100ns=", timestamp);
        WriteUnsignedLine(metadataFile, "process_id=", ::GetCurrentProcessId());
        WriteUnsignedLine(metadataFile, "thread_id=", ::GetCurrentThreadId());
        WriteUnsignedLine(metadataFile, "exception_code=", exceptionCode, true);
        WriteUnsignedLine(metadataFile, "exception_address=", exceptionAddress, true);
        WriteUnsignedLine(metadataFile, "working_set_bytes=", hasProcessMemory != FALSE ? processMemory.WorkingSetSize : 0U);
        WriteUnsignedLine(metadataFile, "private_bytes=", hasProcessMemory != FALSE ? processMemory.PrivateUsage : 0U);
        WriteUnsignedLine(metadataFile, "system_memory_load_percent=", hasSystemMemory != FALSE ? systemMemory.dwMemoryLoad : 0U);
        WriteUnsignedLine(metadataFile, "system_physical_total_bytes=", hasSystemMemory != FALSE ? systemMemory.ullTotalPhys : 0U);
        WriteUnsignedLine(metadataFile, "system_physical_available_bytes=", hasSystemMemory != FALSE ? systemMemory.ullAvailPhys : 0U);
        WriteUnsignedLine(metadataFile, "dump_written=", dumpWritten != FALSE ? 1U : 0U);
        WriteUnsignedLine(metadataFile, "dump_win32_error=", dumpError);
        WriteUnsignedLine(metadataFile, "dump_bytes=", dumpBytes);
        (void)WriteLiteral(
            metadataFile,
            "dump_memory_policy=memory-map-and-heap-metadata-plus-stack-referenced-pages;not-full-process-memory\r\n"
            "dump_flags=FullMemoryInfo,ThreadInfo,UnloadedModules,HandleData,ProcessThreadData,ScanMemory,IndirectlyReferencedMemory\r\n"
        );
        (void)::FlushFileBuffers(metadataFile);
        (void)::CloseHandle(metadataFile);
    }
#endif // defined(_WIN32)

#if !defined(_WIN32)
    void CopyDiagnosticFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
        std::ifstream input(source, std::ios::binary);
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (input && output) {
            output << input.rdbuf();
        }
    }

    void WritePosixContextSummary(const std::filesystem::path& artifactDirectory) {
        std::ofstream output(artifactDirectory / "core-context.txt", std::ios::binary | std::ios::trunc);
        if (!output) {
            return;
        }

        const char* runId = ReadFirstEnvironmentValue("FOUNDATION_RUN_ID", "FOUNDATION_MONITOR_RUN_ID");
        const char* generation = ReadFirstEnvironmentValue("FOUNDATION_RUN_GENERATION", "FOUNDATION_MONITOR_GENERATION");
        if (generation == nullptr) {
            generation = ReadFirstEnvironmentValue("FOUNDATION_GENERATION");
        }

        struct rlimit coreLimit = {};
        const int limitStatus = ::getrlimit(RLIMIT_CORE, &coreLimit);
        output << "format_version=1\n"
            << "artifact_kind=posix_core_context\n"
            << "run_id=" << (runId == nullptr ? "unmanaged" : runId) << '\n'
            << "generation=" << (generation == nullptr ? "unknown" : generation) << '\n'
            << "process_id=" << static_cast<unsigned long long>(::getpid()) << '\n'
            << "rlimit_core_read_status=" << limitStatus << '\n';
        if (limitStatus == 0) {
            output << "rlimit_core_soft=" << static_cast<unsigned long long>(coreLimit.rlim_cur) << '\n'
                << "rlimit_core_hard=" << static_cast<unsigned long long>(coreLimit.rlim_max) << '\n';
        }
#if defined(Linux) || defined(__linux__)
        output << "core_delivery_note=The actual core location follows /proc/sys/kernel/core_pattern; a pipe pattern is commonly handled by systemd-coredump.\n";
#else
        output << "core_delivery_note=The actual core location and retention are controlled by the operating system core-dump policy.\n";
#endif
        output << "signal_policy=XDumper does not intercept fatal POSIX signals; the kernel retains native core-dump semantics.\n";
    }
#endif // !defined(_WIN32)
}

namespace FoundationUtils {
    XDumper& XDumper::GetInstance() {
        static XDumper s_Dumper;
        return s_Dumper;
    }

    void XDumper::EnsureDumpDirectory() {
#if defined(_WIN32)
        const std::filesystem::path dumpPath(s_DumpNameWindows);
#else
        const std::filesystem::path dumpPath = std::filesystem::u8path(s_DumpName);
#endif
        const std::filesystem::path dumpDirectory = dumpPath.parent_path();
        if (dumpDirectory.empty()) {
            return;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(dumpDirectory, errorCode);
    }

    XDumper::~XDumper() {
#if defined(_WIN32)
        (void)::SetUnhandledExceptionFilter(_PreviousFilter);
        _PreviousFilter = nullptr;
#endif // defined(_WIN32)
    }

    XDumper::XDumper()
#if defined(_WIN32)
        : _PreviousFilter(nullptr)
#endif // defined(_WIN32)
    {
#if defined(_WIN32)
        InitializeWindowsArtifactPaths();
#else
        const std::filesystem::path artifactDirectory = BuildPerRunArtifactDirectory();
        (void)CopyPathToBuffer(artifactDirectory, s_ArtifactDirectoryName, sizeof(s_ArtifactDirectoryName));
        (void)CopyPathToBuffer(artifactDirectory / "core", s_DumpName, sizeof(s_DumpName));
#endif
        EnsureDumpDirectory();

#if defined(_WIN32)
        _PreviousFilter = ::SetUnhandledExceptionFilter(XDumper::SnUnhandledExceptionFilter);
#endif // defined(_WIN32)

#if !defined(_WIN32)
        EnableCoreDump();
        PreparePosixCrashContext();
#endif // !defined(_WIN32)
    }

    void XDumper::SetDumpFilename(const char* dumpName) {
        if (dumpName == nullptr || dumpName[0] == '\0') {
            return;
        }

#if defined(_WIN32)
        wchar_t wideDumpName[s_PathCapacity] = {};
        if (!ConvertUtf8ToWide(dumpName, wideDumpName, std::size(wideDumpName))) {
            return;
        }

        if (s_HasConfiguredArtifactRoot) {
            (void)::StringCchCopyW(s_DumpNameWindows, std::size(s_DumpNameWindows), s_RunArtifactDirectoryWindows);
            (void)AppendPathComponent(
                s_DumpNameWindows,
                std::size(s_DumpNameWindows),
                FindFileName(wideDumpName)
            );
        }
        else {
            (void)::StringCchCopyW(s_DumpNameWindows, std::size(s_DumpNameWindows), wideDumpName);
        }
#else
        const std::size_t dumpNameLength = std::strlen(dumpName);
        if (dumpNameLength >= sizeof(s_DumpName)) {
            return;
        }
        std::memcpy(s_DumpName, dumpName, dumpNameLength + 1U);
#endif
        EnsureDumpDirectory();
    }

#if defined(_WIN32)
    LONG __stdcall XDumper::SnUnhandledExceptionFilter(PEXCEPTION_POINTERS exceptionInfo) {
        if (::InterlockedCompareExchange(&s_DumpInProgress, 1, 0) == 0) {
            XDumper::CreateMinidump(exceptionInfo);
        }

        // Let the process terminate through the native unhandled-exception path.
        // Calling exit() here would run unsafe teardown code and overwrite the
        // exception's natural termination semantics.
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void XDumper::CreateMinidump(EXCEPTION_POINTERS* exceptionPointers) {
        s_ActualDumpPathWindows[0] = L'\0';
        DWORD dumpError = ERROR_SUCCESS;
        HANDLE dumpFile = CreateUniqueDumpFile(
            s_ActualDumpPathWindows,
            std::size(s_ActualDumpPathWindows),
            &dumpError
        );

        BOOL dumpWritten = FALSE;
        std::uint64_t dumpBytes = 0U;
        FILETIME fileTime = {};
        ::GetSystemTimeAsFileTime(&fileTime);
        ULARGE_INTEGER timestamp = {};
        timestamp.LowPart = fileTime.dwLowDateTime;
        timestamp.HighPart = fileTime.dwHighDateTime;

        if (dumpFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exceptionInformation = {};
            exceptionInformation.ThreadId = ::GetCurrentThreadId();
            exceptionInformation.ExceptionPointers = exceptionPointers;
            exceptionInformation.ClientPointers = FALSE;

            // FullMemoryInfo records the complete virtual-memory map without
            // copying every committed page. ScanMemory plus indirectly referenced
            // pages preserves crash-adjacent heap data, so size grows with the
            // reachable object graph but remains below a MiniDumpWithFullMemory dump.
            constexpr MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithFullMemoryInfo |
                MiniDumpWithThreadInfo |
                MiniDumpWithUnloadedModules |
                MiniDumpWithHandleData |
                MiniDumpWithProcessThreadData |
                MiniDumpScanMemory |
                MiniDumpWithIndirectlyReferencedMemory
                );
            dumpWritten = ::MiniDumpWriteDump(
                ::GetCurrentProcess(),
                ::GetCurrentProcessId(),
                dumpFile,
                dumpType,
                exceptionPointers != nullptr ? &exceptionInformation : nullptr,
                nullptr,
                nullptr
            );
            if (dumpWritten == FALSE) {
                dumpError = ::GetLastError();
            }

            LARGE_INTEGER fileSize = {};
            if (::GetFileSizeEx(dumpFile, &fileSize) != FALSE && fileSize.QuadPart > 0) {
                dumpBytes = static_cast<std::uint64_t>(fileSize.QuadPart);
            }
            (void)::FlushFileBuffers(dumpFile);
            (void)::CloseHandle(dumpFile);
        }

        WriteWindowsMetadata(
            s_ActualDumpPathWindows[0] == L'\0' ? s_DumpNameWindows : s_ActualDumpPathWindows,
            exceptionPointers,
            dumpWritten,
            dumpError,
            dumpBytes,
            timestamp.QuadPart
        );
    }
#endif // defined(_WIN32)

#if !defined(_WIN32)
    void XDumper::EnableCoreDump() {
        struct rlimit limit = {};
        if (::getrlimit(RLIMIT_CORE, &limit) == 0) {
            limit.rlim_cur = limit.rlim_max;
            (void)::setrlimit(RLIMIT_CORE, &limit);
        }

#if defined(__linux__)
        (void)::prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif // defined(__linux__)
    }

    void XDumper::PreparePosixCrashContext() {
        if (s_ArtifactDirectoryName[0] == '\0') {
            return;
        }

        const std::filesystem::path artifactDirectory = std::filesystem::u8path(s_ArtifactDirectoryName);
        std::error_code errorCode;
        std::filesystem::create_directories(artifactDirectory, errorCode);
        if (errorCode) {
            return;
        }

        WritePosixContextSummary(artifactDirectory);
#if defined(Linux) || defined(__linux__)
        CopyDiagnosticFile("/proc/sys/kernel/core_pattern", artifactDirectory / "core_pattern.txt");
        CopyDiagnosticFile("/proc/self/status", artifactDirectory / "proc-status.txt");
        CopyDiagnosticFile("/proc/self/maps", artifactDirectory / "proc-maps.txt");
        CopyDiagnosticFile("/proc/self/limits", artifactDirectory / "proc-limits.txt");
        CopyDiagnosticFile("/proc/self/coredump_filter", artifactDirectory / "proc-coredump-filter.txt");
#endif // defined(Linux) || defined(__linux__)
    }
#endif // !defined(_WIN32)
}
#endif // defined(FOUNDATION_UTILS_XDUMPER_BUILD)

#endif // __XDumper_h__



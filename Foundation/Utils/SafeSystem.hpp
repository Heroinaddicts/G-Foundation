#ifndef FOUNDATION_UTILS_SAFE_SYSTEM_HPP
#define FOUNDATION_UTILS_SAFE_SYSTEM_HPP

#include "MultiSys.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace FoundationUtils {
    namespace SafeSystem {
        namespace Hardware {
            struct HostResourceCounters final {
                bool _CpuAvailable = false;
                UInt64 _CpuTotalTicks = 0;
                UInt64 _CpuIdleTicks = 0;
                bool _MemoryAvailable = false;
                UInt64 _MemoryTotalBytes = 0;
                UInt64 _MemoryAvailableBytes = 0;
                bool _DiskAvailable = false;
                UInt64 _DiskTotalBytes = 0;
                UInt64 _DiskAvailableBytes = 0;
            };

            int NumberOfCpuCores();
            bool ReadHostResourceCounters(const std::string& diskPath, HostResourceCounters& counters);
        }

        namespace Network {
            inline constexpr UInt64 Byte = 1;
            inline constexpr UInt64 KB = 1024;
            inline constexpr UInt64 MB = 1024 * KB;
            inline constexpr UInt64 GB = 1024 * MB;
            inline constexpr UInt64 TB = 1024 * GB;
            inline constexpr UInt64 PB = 1024 * TB;
        }

        namespace Time {
            enum : int {
                Second = 1000,
                Minute = 60 * Second,
                Hour = 60 * Minute,
                Day = 24 * Hour
            };

            bool SafeLocaltime(std::tm& time, std::time_t timestamp);
            void MillisecondSleep(int milliseconds);

            __forceinline UInt64 GetMicroSecond() {
                return static_cast<UInt64>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
            }

            __forceinline UInt64 GetMilliSecond() {
                return static_cast<UInt64>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            }

            std::string GetCurrentTimeString(const char* format = "%4d-%02d-%02d %02d:%02d:%02d");
            std::string GetTimeStringByMillisecond(Int64 timestamp, const char* format = "%4d-%02d-%02d %02d:%02d:%02d");
        }

        namespace Process {
            inline constexpr UInt64 InvalidID = std::numeric_limits<UInt64>::max();

            struct DetachedProcessResult final {
                UInt64 _ProcessId = InvalidID;
                std::string _Diagnostic;
            };

            UInt64 GetCurrentProcessID();
            UInt64 GetCurrentThreadID();
            UInt64 LaunchProcess(const std::string& executablePath, const std::string& arguments = "");
            bool LaunchDetachedProcess(
                const std::string& executablePath,
                const std::vector<std::string>& arguments,
                const std::string& workingDirectory,
                bool createNewConsole,
                DetachedProcessResult& result);
        }

        namespace File {
            // All filesystem path strings in this namespace are UTF-8. Convert
            // to std::filesystem::path at the platform boundary.
            __forceinline std::filesystem::path PathFromUtf8(const std::string& value) {
                return std::filesystem::path(std::u8string(
                    reinterpret_cast<const char8_t*>(value.data()),
                    value.size()
                ));
            }

            const std::string& GetCurrentExecutablePath();
            const std::string& GetDirectoryOfCurrentExe();

            __forceinline std::string LoadFileAsString(const std::string& path) {
                std::ifstream file(PathFromUtf8(path), std::ios::in | std::ios::binary);
                if (!file.is_open()) {
                    return "";
                }

                std::stringstream content;
                content << file.rdbuf();
                return content.str();
            }

            __forceinline bool SaveStringToFile(const std::string& path, const std::string& content) {
                const std::filesystem::path filePath = PathFromUtf8(path);
                const std::filesystem::path directory = filePath.parent_path();
                std::error_code error;
                if (!directory.empty() && !std::filesystem::exists(directory) && !std::filesystem::create_directories(directory, error)) {
                    return false;
                }

                std::ofstream file(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!file.is_open()) {
                    return false;
                }

                const unsigned char utf8Bom[] = { 0xEF, 0xBB, 0xBF };
                file.write(reinterpret_cast<const char*>(utf8Bom), sizeof(utf8Bom));
                file.write(content.c_str(), static_cast<std::streamsize>(content.size()));
                return file.good();
            }

            bool FileExists(const std::string& path);
            bool FolderExists(const std::string& path);
            bool CreateFolder(const std::string& path);
            bool DelFolder(const std::string& path);
            bool DelFile(const std::string& path);

            struct FileInfo {
                const bool _IsDirectory;
                const std::string _ParentDirectory;
                const std::string _Name;
                const std::string _Externsion;

                FileInfo(const bool isDirectory, const std::string & dir, const std::string & name, const std::string & externsion)
                    : _IsDirectory(isDirectory), _ParentDirectory(dir), _Name(name), _Externsion(externsion) {}
            };

            // Append files and directories; return the number of entries appended.
            // _Name includes the file extension; directories have an empty _Externsion.
            // Extension filters apply only to files. Rejected directories are not traversed.
            int GetFileInDirectory(const std::string& directory, const std::string& extension, OUT std::vector<FileInfo>& files, bool recursive = true, const std::set<std::string>& rejectDirectories = {}, const std::set<std::string>& rejectExtensions = {});
        }
    }
}

#if defined(FOUNDATION_UTILS_SAFE_SYSTEM_BUILD)
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <unistd.h>
extern char** environ;
#elif defined(__linux__)
#include <spawn.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>
extern char** environ;
#endif

namespace FoundationUtils {
    namespace SafeSystem {
        namespace Hardware {
            int NumberOfCpuCores() {
                const unsigned int count = std::thread::hardware_concurrency();
                return count > 0 ? static_cast<int>(count) : 1;
            }

            bool ReadHostResourceCounters(const std::string& diskPath, HostResourceCounters& counters) {
                counters = HostResourceCounters();

#if defined(_WIN32)
                FILETIME idleTime = {};
                FILETIME kernelTime = {};
                FILETIME userTime = {};
                if (::GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
                    const ULARGE_INTEGER idle = { idleTime.dwLowDateTime, idleTime.dwHighDateTime };
                    const ULARGE_INTEGER kernel = { kernelTime.dwLowDateTime, kernelTime.dwHighDateTime };
                    const ULARGE_INTEGER user = { userTime.dwLowDateTime, userTime.dwHighDateTime };
                    counters._CpuAvailable = true;
                    counters._CpuIdleTicks = static_cast<UInt64>(idle.QuadPart);
                    counters._CpuTotalTicks = static_cast<UInt64>(kernel.QuadPart + user.QuadPart);
                }

                MEMORYSTATUSEX memoryStatus = {};
                memoryStatus.dwLength = sizeof(memoryStatus);
                if (::GlobalMemoryStatusEx(&memoryStatus)) {
                    counters._MemoryAvailable = true;
                    counters._MemoryTotalBytes = static_cast<UInt64>(memoryStatus.ullTotalPhys);
                    counters._MemoryAvailableBytes = static_cast<UInt64>(memoryStatus.ullAvailPhys);
                }

                if (!diskPath.empty()) {
                    const std::wstring nativeDiskPath = File::PathFromUtf8(diskPath).wstring();
                    ULARGE_INTEGER availableBytes = {};
                    ULARGE_INTEGER totalBytes = {};
                    if (::GetDiskFreeSpaceExW(nativeDiskPath.c_str(), &availableBytes, &totalBytes, nullptr)) {
                        counters._DiskAvailable = true;
                        counters._DiskTotalBytes = static_cast<UInt64>(totalBytes.QuadPart);
                        counters._DiskAvailableBytes = static_cast<UInt64>(availableBytes.QuadPart);
                    }
                }
#elif defined(__linux__)
                std::ifstream cpuFile("/proc/stat");
                std::string cpuLabel;
                UInt64 user = 0;
                UInt64 nice = 0;
                UInt64 system = 0;
                UInt64 idle = 0;
                UInt64 ioWait = 0;
                UInt64 irq = 0;
                UInt64 softIrq = 0;
                UInt64 steal = 0;
                if (cpuFile >> cpuLabel >> user >> nice >> system >> idle >> ioWait >> irq >> softIrq >> steal && cpuLabel == "cpu") {
                    counters._CpuAvailable = true;
                    counters._CpuIdleTicks = idle + ioWait;
                    counters._CpuTotalTicks = user + nice + system + idle + ioWait + irq + softIrq + steal;
                }

                std::ifstream memoryFile("/proc/meminfo");
                std::string memoryKey;
                UInt64 memoryValue = 0;
                std::string memoryUnit;
                while (memoryFile >> memoryKey >> memoryValue >> memoryUnit) {
                    if (memoryKey == "MemTotal:") {
                        counters._MemoryTotalBytes = memoryValue * 1024;
                    } else if (memoryKey == "MemAvailable:") {
                        counters._MemoryAvailableBytes = memoryValue * 1024;
                    }
                }
                counters._MemoryAvailable = counters._MemoryTotalBytes != 0;

                if (!diskPath.empty()) {
                    struct statvfs diskStatus = {};
                    if (::statvfs(diskPath.c_str(), &diskStatus) == 0) {
                        counters._DiskAvailable = true;
                        counters._DiskTotalBytes = static_cast<UInt64>(diskStatus.f_blocks) * static_cast<UInt64>(diskStatus.f_frsize);
                        counters._DiskAvailableBytes = static_cast<UInt64>(diskStatus.f_bavail) * static_cast<UInt64>(diskStatus.f_frsize);
                    }
                }
#elif defined(__APPLE__)
                host_cpu_load_info_data_t cpuLoad = {};
                mach_msg_type_number_t cpuLoadCount = HOST_CPU_LOAD_INFO_COUNT;
                if (::host_statistics(::mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpuLoad), &cpuLoadCount) == KERN_SUCCESS) {
                    counters._CpuAvailable = true;
                    counters._CpuIdleTicks = static_cast<UInt64>(cpuLoad.cpu_ticks[CPU_STATE_IDLE]);
                    counters._CpuTotalTicks = static_cast<UInt64>(cpuLoad.cpu_ticks[CPU_STATE_USER])
                        + static_cast<UInt64>(cpuLoad.cpu_ticks[CPU_STATE_SYSTEM])
                        + static_cast<UInt64>(cpuLoad.cpu_ticks[CPU_STATE_IDLE])
                        + static_cast<UInt64>(cpuLoad.cpu_ticks[CPU_STATE_NICE]);
                }

                std::uint64_t totalMemoryBytes = 0;
                std::size_t totalMemorySize = sizeof(totalMemoryBytes);
                vm_statistics64_data_t virtualMemory = {};
                mach_msg_type_number_t virtualMemoryCount = HOST_VM_INFO64_COUNT;
                vm_size_t pageSize = 0;
                if (::sysctlbyname("hw.memsize", &totalMemoryBytes, &totalMemorySize, nullptr, 0) == 0
                    && ::host_page_size(::mach_host_self(), &pageSize) == KERN_SUCCESS
                    && ::host_statistics64(::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&virtualMemory), &virtualMemoryCount) == KERN_SUCCESS) {
                    counters._MemoryAvailable = true;
                    counters._MemoryTotalBytes = static_cast<UInt64>(totalMemoryBytes);
                    counters._MemoryAvailableBytes = (static_cast<UInt64>(virtualMemory.free_count)
                        + static_cast<UInt64>(virtualMemory.inactive_count)) * static_cast<UInt64>(pageSize);
                }

                if (!diskPath.empty()) {
                    struct statvfs diskStatus = {};
                    if (::statvfs(diskPath.c_str(), &diskStatus) == 0) {
                        counters._DiskAvailable = true;
                        counters._DiskTotalBytes = static_cast<UInt64>(diskStatus.f_blocks) * static_cast<UInt64>(diskStatus.f_frsize);
                        counters._DiskAvailableBytes = static_cast<UInt64>(diskStatus.f_bavail) * static_cast<UInt64>(diskStatus.f_frsize);
                    }
                }
#endif

                return counters._CpuAvailable || counters._MemoryAvailable || counters._DiskAvailable;
            }
        }

        namespace Time {
            bool SafeLocaltime(std::tm& time, const std::time_t timestamp) {
#if defined(_WIN32)
                return localtime_s(&time, &timestamp) == 0;
#else
                return localtime_r(&timestamp, &time) != nullptr;
#endif
            }

            void MillisecondSleep(const int milliseconds) {
                if (milliseconds > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
                }
            }

            std::string GetCurrentTimeString(const char* format) {
                return GetTimeStringByMillisecond(static_cast<Int64>(GetMilliSecond()), format);
            }

            std::string GetTimeStringByMillisecond(const Int64 timestamp, const char* format) {
                const std::chrono::milliseconds milliseconds(timestamp);
                const std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> timePoint(milliseconds);
                const std::time_t rawTime = std::chrono::system_clock::to_time_t(timePoint);
                std::tm localTime = {};
                if (!SafeLocaltime(localTime, rawTime)) {
                    return "";
                }

                char content[128] = {};
                std::snprintf(content, sizeof(content), format, localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday, localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
                return content;
            }
        }

        namespace Process {
#if defined(_WIN32)
            std::wstring QuoteWindowsProcessArgument(const std::wstring& argument) {
                if (!argument.empty()
                    && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
                    return argument;
                }

                std::wstring quotedArgument(1, L'\"');
                std::size_t slashCount = 0;
                for (const wchar_t character : argument) {
                    if (character == L'\\') {
                        ++slashCount;
                        continue;
                    }
                    if (character == L'\"') {
                        quotedArgument.append(slashCount * 2 + 1, L'\\');
                        quotedArgument.push_back(character);
                        slashCount = 0;
                        continue;
                    }
                    quotedArgument.append(slashCount, L'\\');
                    slashCount = 0;
                    quotedArgument.push_back(character);
                }
                quotedArgument.append(slashCount * 2, L'\\');
                quotedArgument.push_back(L'\"');
                return quotedArgument;
            }

            std::wstring Utf8ProcessTextToWide(const std::string& value) {
                return File::PathFromUtf8(value).wstring();
            }
#endif

            UInt64 GetCurrentProcessID() {
#if defined(_WIN32)
                return static_cast<UInt64>(::GetCurrentProcessId());
#else
                return static_cast<UInt64>(::getpid());
#endif
            }

            UInt64 GetCurrentThreadID() {
#if defined(_WIN32)
                return static_cast<UInt64>(::GetCurrentThreadId());
#elif defined(__APPLE__)
                UInt64 threadID = 0;
                return pthread_threadid_np(nullptr, &threadID) == 0 ? threadID : InvalidID;
#else
                return static_cast<UInt64>(::syscall(SYS_gettid));
#endif
            }

            UInt64 LaunchProcess(const std::string& executablePath, const std::string& arguments) {
#if defined(_WIN32)
                STARTUPINFOA startupInfo = {};
                startupInfo.cb = sizeof(startupInfo);
                PROCESS_INFORMATION processInformation = {};
                std::vector<char> commandLine(arguments.begin(), arguments.end());
                commandLine.push_back('\0');

                const BOOL launched = ::CreateProcessA(executablePath.c_str(), arguments.empty() ? nullptr : commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInformation);
                if (!launched) {
                    return InvalidID;
                }

                ::CloseHandle(processInformation.hThread);
                return static_cast<UInt64>(reinterpret_cast<std::uintptr_t>(processInformation.hProcess));
#else
                std::vector<char*> argumentValues;
                argumentValues.push_back(const_cast<char*>(executablePath.c_str()));
                if (!arguments.empty()) {
                    argumentValues.push_back(const_cast<char*>(arguments.c_str()));
                }
                argumentValues.push_back(nullptr);

                pid_t processID = 0;
                return posix_spawnp(&processID, executablePath.c_str(), nullptr, nullptr, argumentValues.data(), environ) == 0 ? static_cast<UInt64>(processID) : InvalidID;
#endif
            }

            bool LaunchDetachedProcess(
                const std::string& executablePath,
                const std::vector<std::string>& arguments,
                const std::string& workingDirectory,
                const bool createNewConsole,
                DetachedProcessResult& result) {
                result = DetachedProcessResult();
                if (executablePath.empty()) {
                    result._Diagnostic = "The detached process executable is empty.";
                    return false;
                }

#if defined(_WIN32)
                const std::wstring executable = Utf8ProcessTextToWide(executablePath);
                std::wstring commandLine = QuoteWindowsProcessArgument(executable);
                for (const std::string& argument : arguments) {
                    commandLine.push_back(L' ');
                    commandLine += QuoteWindowsProcessArgument(Utf8ProcessTextToWide(argument));
                }
                std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
                mutableCommandLine.push_back(L'\0');

                const std::wstring nativeWorkingDirectory = workingDirectory.empty()
                    ? std::wstring()
                    : Utf8ProcessTextToWide(workingDirectory);
                STARTUPINFOW startupInfo = {};
                startupInfo.cb = sizeof(startupInfo);
                PROCESS_INFORMATION processInformation = {};
                const DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT
                    | (createNewConsole ? CREATE_NEW_CONSOLE : CREATE_NEW_PROCESS_GROUP);
                const BOOL launched = ::CreateProcessW(
                    nullptr,
                    mutableCommandLine.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    creationFlags,
                    nullptr,
                    nativeWorkingDirectory.empty() ? nullptr : nativeWorkingDirectory.c_str(),
                    &startupInfo,
                    &processInformation);
                if (!launched) {
                    result._Diagnostic = "CreateProcessW failed with Windows error "
                        + std::to_string(static_cast<unsigned long>(::GetLastError())) + ".";
                    return false;
                }

                result._ProcessId = static_cast<UInt64>(processInformation.dwProcessId);
                ::CloseHandle(processInformation.hThread);
                ::CloseHandle(processInformation.hProcess);
                return true;
#else
                static_cast<void>(arguments);
                static_cast<void>(workingDirectory);
                static_cast<void>(createNewConsole);
                result._Diagnostic = "Detached process launch is not implemented on this platform.";
                return false;
#endif
            }
        }

        namespace File {
            std::string PathToUtf8(const std::filesystem::path& path) {
                const std::u8string value = path.generic_u8string();
                return std::string(reinterpret_cast<const char*>(value.data()), value.size());
            }

            std::string ResolveCurrentExecutablePath() {
#if defined(_WIN32)
                std::vector<wchar_t> path(32768, L'\0');
                const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0 || length >= path.size()) {
                    return {};
                }
                return PathToUtf8(std::filesystem::path(std::wstring(path.data(), length)));
#elif defined(__APPLE__)
                std::uint32_t size = 0;
                _NSGetExecutablePath(nullptr, &size);
                std::vector<char> path(size, '\0');
                return _NSGetExecutablePath(path.data(), &size) == 0
                    ? PathToUtf8(std::filesystem::weakly_canonical(path.data()))
                    : std::string();
#else
                std::vector<char> path(4096, '\0');
                const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
                return length > 0 ? std::string(path.data(), static_cast<std::size_t>(length)) : std::string();
#endif
            }

            bool IsExtensionAccepted(const std::filesystem::path& path, const std::string& extension, const std::set<std::string>& rejectExtensions) {
                const std::string fileExtension = PathToUtf8(path.extension());
                return (extension == "*" || fileExtension == extension) && rejectExtensions.find(fileExtension) == rejectExtensions.end();
            }

            void AppendFile(const std::filesystem::path& path, const bool isDirectory, std::vector<FileInfo>& files) {
                files.emplace_back(isDirectory, PathToUtf8(path.parent_path()),
                    PathToUtf8(path.filename()), isDirectory ? std::string() : PathToUtf8(path.extension()));
            }

            const std::string& GetCurrentExecutablePath() {
                static const std::string path = ResolveCurrentExecutablePath();
                return path;
            }

            const std::string& GetDirectoryOfCurrentExe() {
                static const std::string directory = PathToUtf8(PathFromUtf8(GetCurrentExecutablePath()).parent_path());
                return directory;
            }

            bool FileExists(const std::string& path) {
                std::error_code error;
                return std::filesystem::is_regular_file(PathFromUtf8(path), error);
            }

            bool FolderExists(const std::string& path) {
                std::error_code error;
                return std::filesystem::is_directory(PathFromUtf8(path), error);
            }

            bool CreateFolder(const std::string& path) {
                std::error_code error;
                const std::filesystem::path folderPath = PathFromUtf8(path);
                return std::filesystem::is_directory(folderPath, error) || std::filesystem::create_directories(folderPath, error);
            }

            bool DelFolder(const std::string& path) {
                std::error_code error;
                const std::filesystem::path folderPath = PathFromUtf8(path);
                if (!std::filesystem::exists(folderPath, error)) {
                    return true;
                }
                std::filesystem::remove_all(folderPath, error);
                return !error;
            }

            bool DelFile(const std::string& path) {
                std::error_code error;
                const std::filesystem::path filePath = PathFromUtf8(path);
                return !std::filesystem::exists(filePath, error) || std::filesystem::remove(filePath, error);
            }

            int GetFileInDirectory(const std::string& directory, const std::string& extension, std::vector<FileInfo>& files, const bool recursive, const std::set<std::string>& rejectDirectories, const std::set<std::string>& rejectExtensions) {
                std::error_code error;
                const std::filesystem::path root = PathFromUtf8(directory);
                if (!std::filesystem::is_directory(root, error)) {
                    return 0;
                }

                const std::size_t initialCount = files.size();
                if (recursive) {
                    std::filesystem::recursive_directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
                    const std::filesystem::recursive_directory_iterator end;
                    while (!error && iterator != end) {
                        const std::filesystem::directory_entry& entry = *iterator;
                        if (entry.is_directory(error)) {
                            if (rejectDirectories.find(PathToUtf8(entry.path().filename())) != rejectDirectories.end()) {
                                iterator.disable_recursion_pending();
                            }
                            else {
                                AppendFile(entry.path(), true, files);
                            }
                        }
                        else if (entry.is_regular_file(error) && IsExtensionAccepted(entry.path(), extension, rejectExtensions)) {
                            AppendFile(entry.path(), false, files);
                        }
                        iterator.increment(error);
                    }
                }
                else {
                    std::filesystem::directory_iterator iterator(root, std::filesystem::directory_options::skip_permission_denied, error);
                    const std::filesystem::directory_iterator end;
                    while (!error && iterator != end) {
                        const std::filesystem::directory_entry& entry = *iterator;
                        if (entry.is_directory(error)) {
                            if (rejectDirectories.find(PathToUtf8(entry.path().filename())) == rejectDirectories.end()) {
                                AppendFile(entry.path(), true, files);
                            }
                        }
                        else if (entry.is_regular_file(error) && IsExtensionAccepted(entry.path(), extension, rejectExtensions)) {
                            AppendFile(entry.path(), false, files);
                        }
                        iterator.increment(error);
                    }
                }
                return static_cast<int>(files.size() - initialCount);
            }
        }
    }
}
#endif // defined(FOUNDATION_UTILS_SAFE_SYSTEM_BUILD)

#endif // FOUNDATION_UTILS_SAFE_SYSTEM_HPP



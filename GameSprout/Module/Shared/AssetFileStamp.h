#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

// A cheap observation of one filesystem object, not a content fingerprint.
// Identity and change time also detect replacement or a write that restores mtime.
struct AssetFileStamp {
    bool Exists = false;
    bool Directory = false;
    uint64_t Size = 0;
    int64_t ModifiedNanoseconds = 0;
    int64_t ChangedNanoseconds = 0;
    uint64_t Device = 0;
    uint64_t FileID = 0;

    bool operator==(const AssetFileStamp &) const = default;
};

namespace AssetFileStampDetail {
#if defined(_WIN32)
inline bool WindowsTimeToNanoseconds(int64_t ticks, int64_t & nanoseconds) {
    // FILETIME uses 100ns ticks from 1601; our stored times use Unix nanoseconds.
    constexpr int64_t epochOffset = 116444736000000000LL;
    constexpr int64_t minimumTicks = epochOffset + (std::numeric_limits<int64_t>::min)() / 100;
    constexpr int64_t maximumTicks = epochOffset + (std::numeric_limits<int64_t>::max)() / 100;
    if (ticks < minimumTicks || ticks > maximumTicks) return false;
    nanoseconds = (ticks - epochOffset) * 100;
    return true;
}
#else
inline bool PosixTimeToNanoseconds(int64_t seconds, int64_t fraction, int64_t & nanoseconds) {
    constexpr int64_t scale = 1000000000;
    constexpr int64_t minimum = (std::numeric_limits<int64_t>::min)();
    constexpr int64_t maximum = (std::numeric_limits<int64_t>::max)();
    if (fraction < 0 || fraction >= scale || seconds > maximum / scale
        || seconds < minimum / scale - 1) return false;
    // A negative timespec uses floor(seconds), so one additional second can fit
    // after adding its nonnegative fraction. Avoid overflowing the intermediate.
    if (seconds == minimum / scale - 1) {
        constexpr int64_t minimumFraction = scale + minimum % scale;
        if (fraction < minimumFraction) return false;
        nanoseconds = minimum + (fraction - minimumFraction);
        return true;
    }
    const int64_t whole = seconds * scale;
    if (whole > maximum - fraction) return false;
    nanoseconds = whole + fraction;
    return true;
}
#endif
}

// Returns true for a missing path with Exists=false. Permission/I/O errors,
// symlinks, reparse points and non-file/non-directory objects return false.
// The output is cleared on failure. No file contents are read or modified.
inline bool ReadAssetFileStamp(const std::string & absolutePath, AssetFileStamp & stamp) {
    stamp = {};
    if (absolutePath.empty() || absolutePath.find('\0') != std::string::npos) return false;
    AssetFileStamp observed;
#if defined(_WIN32)
    if (absolutePath.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) return false;
    const int byteCount = static_cast<int>(absolutePath.size());
    const int characterCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        absolutePath.data(), byteCount, nullptr, 0);
    if (characterCount <= 0) return false;
    std::wstring nativePath(static_cast<std::size_t>(characterCount), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, absolutePath.data(), byteCount,
        nativePath.data(), characterCount) != characterCount) return false;

    const HANDLE file = CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_DIRECTORY;
    }
    BY_HANDLE_FILE_INFORMATION identity{};
    FILE_BASIC_INFO basic{};
    const bool read = GetFileType(file) == FILE_TYPE_DISK
        && GetFileInformationByHandle(file, &identity)
        && GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic));
    CloseHandle(file);
    if (!read || (basic.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))) return false;
    observed.Exists = true;
    observed.Directory = (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!observed.Directory)
        observed.Size = (static_cast<uint64_t>(identity.nFileSizeHigh) << 32) | identity.nFileSizeLow;
    observed.Device = identity.dwVolumeSerialNumber;
    observed.FileID = (static_cast<uint64_t>(identity.nFileIndexHigh) << 32) | identity.nFileIndexLow;
    if (!AssetFileStampDetail::WindowsTimeToNanoseconds(basic.LastWriteTime.QuadPart, observed.ModifiedNanoseconds)
        || !AssetFileStampDetail::WindowsTimeToNanoseconds(basic.ChangeTime.QuadPart, observed.ChangedNanoseconds)) return false;
#else
    struct stat information{};
    if (lstat(absolutePath.c_str(), &information) != 0) return errno == ENOENT || errno == ENOTDIR;
    if (!S_ISREG(information.st_mode) && !S_ISDIR(information.st_mode)) return false;
    observed.Exists = true;
    observed.Directory = S_ISDIR(information.st_mode);
    if (!observed.Directory) {
        if (information.st_size < 0) return false;
        observed.Size = static_cast<uint64_t>(information.st_size);
    }
    observed.Device = static_cast<uint64_t>(information.st_dev);
    observed.FileID = static_cast<uint64_t>(information.st_ino);
#if defined(__APPLE__)
    const auto modified = information.st_mtimespec;
    const auto changed = information.st_ctimespec;
#else
    const auto modified = information.st_mtim;
    const auto changed = information.st_ctim;
#endif
    if (!AssetFileStampDetail::PosixTimeToNanoseconds(modified.tv_sec, modified.tv_nsec, observed.ModifiedNanoseconds)
        || !AssetFileStampDetail::PosixTimeToNanoseconds(changed.tv_sec, changed.tv_nsec, observed.ChangedNanoseconds)) return false;
#endif
    stamp = observed;
    return true;
}

#include "AssetFileFingerprintCache.h"
#include <QCryptographicHash>
#include <QFile>
#include <chrono>
#include <iterator>

namespace {
constexpr uint64_t kRetainedByteLimit = 32 * 1024 * 1024;
constexpr uint64_t kRetainedFileLimit = 1024 * 1024;
constexpr std::size_t kEntryLimit = 16384;

bool Cancelled(Foundation::ITaskContext * context) {
    return context && context->IsCancellationRequested();
}
}

void AssetFileFingerprintCache::Open() {
    std::lock_guard<std::mutex> lock(_Mutex);
    ++_Epoch;
    _Entries.clear();
    _Order.clear();
    _RetainedBytes = 0;
    _Statistics = {};
    _Open = true;
    _Changed.notify_all();
}

void AssetFileFingerprintCache::Close() {
    std::lock_guard<std::mutex> lock(_Mutex);
    ++_Epoch;
    _Open = false;
    _Entries.clear();
    _Order.clear();
    _RetainedBytes = 0;
    _Changed.notify_all();
}

AssetFileFingerprintCache::Statistics AssetFileFingerprintCache::GetStatistics() const {
    std::lock_guard<std::mutex> lock(_Mutex);
    return _Statistics;
}

bool AssetFileFingerprintCache::Seed(const std::string & path, const AssetFileFingerprint & fingerprint) {
    if (!fingerprint.Version.Exists || fingerprint.Version.Directory || fingerprint.Hash.size() != 64) return false;
    for (const char value : fingerprint.Hash)
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) return false;
    std::lock_guard<std::mutex> lock(_Mutex);
    if (!_Open) return false;
    auto previous = _Entries.find(path);
    if (previous != _Entries.end() && previous->second.Fingerprint.Version == fingerprint.Version) {
        if (previous->second.Pending) return true;
        if (previous->second.Fingerprint.Hash == fingerprint.Hash) return true;
        _RetainedBytes -= static_cast<uint64_t>(previous->second.Bytes.size());
        if (previous->second.InOrder) _Order.erase(previous->second.Order);
        _Entries.erase(previous);
        return false;
    }
    Entry entry;
    entry.Fingerprint = fingerprint;
    entry.Token = ++_NextToken;
    if (previous != _Entries.end()) {
        _RetainedBytes -= static_cast<uint64_t>(previous->second.Bytes.size());
        if (previous->second.InOrder) _Order.erase(previous->second.Order);
        _Entries.erase(previous);
    }
    const auto inserted = _Entries.emplace(path, std::move(entry)).first;
    try {
        _Order.push_back(path);
        inserted->second.Order = std::prev(_Order.end());
        inserted->second.InOrder = true;
    } catch (...) { _Entries.erase(inserted); throw; }
    Trim(0);
    _Changed.notify_all();
    return true;
}

void AssetFileFingerprintCache::Trim(uint64_t protectedToken) {
    std::size_t remaining = _Order.size();
    while ((_RetainedBytes > kRetainedByteLimit || _Entries.size() > kEntryLimit) && remaining-- && !_Order.empty()) {
        const auto order = _Order.begin();
        const auto oldest = _Entries.find(*order);
        if (oldest->second.Token == protectedToken) { _Order.splice(_Order.end(), _Order, order); continue; }
        _RetainedBytes -= static_cast<uint64_t>(oldest->second.Bytes.size());
        if (_Entries.size() > kEntryLimit) { _Entries.erase(oldest); _Order.erase(order); }
        else {
            oldest->second.Bytes.clear(); oldest->second.HasBytes = false;
            _Order.splice(_Order.end(), _Order, order);
        }
    }
}

bool AssetFileFingerprintCache::Read(const std::string & path, QByteArray * output,
    AssetFileFingerprint & fingerprint, std::string & error, bool & changed,
    Foundation::ITaskContext * context) {
    changed = false;
    error.clear();
    fingerprint = {};
    if (output) output->clear();
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        ++_Statistics.Requests;
    }
    for (;;) {
        if (Cancelled(context)) { error = "Import cancelled"; return false; }
        AssetFileStamp before;
        if (!ReadAssetFileStamp(path, before) || !before.Exists || before.Directory) {
            error = "Cannot inspect " + path; return false;
        }
        uint64_t token = 0, epoch = 0;
        std::string knownHash;
        {
            std::unique_lock<std::mutex> lock(_Mutex);
            if (!_Open) { error = "Fingerprint cache is closed"; return false; }
            auto found = _Entries.find(path);
            if (found != _Entries.end() && found->second.Fingerprint.Version == before) {
                if (found->second.Pending) {
                    // 等待单个文件的生产者，不持锁做 IO；取消最迟在下一次短等待后生效。
                    _Changed.wait_for(lock, std::chrono::milliseconds(10));
                    continue;
                }
                ++_Statistics.Hits;
                if (found->second.InOrder) _Order.splice(_Order.end(), _Order, found->second.Order);
                if (!output || found->second.HasBytes) {
                    fingerprint = found->second.Fingerprint;
                    if (output) *output = found->second.Bytes;
                    return true;
                }
                knownHash = found->second.Fingerprint.Hash;
            }
            Entry entry;
            entry.Fingerprint.Version = before;
            entry.Fingerprint.Hash = knownHash;
            entry.Pending = true;
            entry.Token = token = ++_NextToken;
            if (found != _Entries.end()) {
                _RetainedBytes -= static_cast<uint64_t>(found->second.Bytes.size());
                if (found->second.InOrder) _Order.erase(found->second.Order);
                _Entries.erase(found);
            }
            _Entries.emplace(path, std::move(entry));
            epoch = _Epoch;
            ++_Statistics.ContentReads;
            if (knownHash.empty()) ++_Statistics.HashComputations;
        }

        try {
            QByteArray bytes;
            const bool retain = before.Size <= kRetainedFileLimit;
            QCryptographicHash hash(QCryptographicHash::Sha256);
            QFile file(QString::fromStdString(path));
            bool success = file.open(QIODevice::ReadOnly);
            if (!success) error = "Cannot read " + path + ": " + file.errorString().toStdString();
            uint64_t readBytes = 0;
            while (success && !file.atEnd()) {
                if (Cancelled(context)) { error = "Import cancelled"; success = false; break; }
                const QByteArray block = file.read(1024 * 1024);
                if (file.error() != QFileDevice::NoError || (block.isEmpty() && !file.atEnd())) {
                    error = "Read failed: " + path; success = false; break;
                }
                readBytes += static_cast<uint64_t>(block.size());
                if (readBytes > before.Size) {
                    changed = true; error = "File grew while reading: " + path; success = false; break;
                }
                if (knownHash.empty()) hash.addData(block);
                if (output || retain) bytes.append(block);
            }
            AssetFileStamp after;
            if (success && !ReadAssetFileStamp(path, after)) { error = "Cannot inspect " + path; success = false; }
            if (success && (before != after || readBytes != before.Size)) {
                changed = true; error = "File changed while reading: " + path; success = false;
            }
            if (success && Cancelled(context)) { error = "Import cancelled"; success = false; }
            const std::string contentHash = success ? (knownHash.empty() ? hash.result().toHex().toStdString() : knownHash) : "";
            {
                std::lock_guard<std::mutex> lock(_Mutex);
                auto found = _Entries.find(path);
                const bool current = _Open && epoch == _Epoch && found != _Entries.end() && found->second.Token == token;
                if (current && success) {
                    Entry & entry = found->second;
                    entry.Fingerprint = {after, contentHash};
                    entry.Pending = false;
                    _Order.push_back(path);
                    entry.Order = std::prev(_Order.end());
                    entry.InOrder = true;
                    if (retain) {
                        entry.Bytes = bytes;
                        entry.HasBytes = true;
                        _RetainedBytes += static_cast<uint64_t>(bytes.size());
                    }
                    if (knownHash.empty()) _Statistics.BytesHashed += readBytes;
                    Trim(token);
                } else if (current) _Entries.erase(found);
                if (!current && success) { success = false; error = "Fingerprint request was superseded"; changed = true; }
                _Changed.notify_all();
            }
            if (!success) return false;
            fingerprint = {after, contentHash};
            if (output) *output = std::move(bytes);
            return true;
        } catch (...) {
            std::lock_guard<std::mutex> lock(_Mutex);
            const auto found = _Entries.find(path);
            if (found != _Entries.end() && found->second.Token == token) {
                _RetainedBytes -= static_cast<uint64_t>(found->second.Bytes.size());
                if (found->second.InOrder) _Order.erase(found->second.Order);
                _Entries.erase(found);
            }
            _Changed.notify_all();
            throw;
        }
    }
}

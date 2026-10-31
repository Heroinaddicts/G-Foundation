#pragma once

#include "../Shared/AssetFileStamp.h"
#include "ITaskApi.h"
#include <QByteArray>
#include <condition_variable>
#include <map>
#include <list>
#include <mutex>

struct AssetFileFingerprint {
    AssetFileStamp Version;
    std::string Hash;
};

// Pipeline 静态实例直接拥有；Release 只 Close，不销毁缓存对象。
// 模块销毁前宿主必须停止并 join Foundation 工作线程。
// 锁只保护缓存状态，文件读取和哈希计算均在锁外。
class AssetFileFingerprintCache {
public:
    struct Statistics {
        uint64_t Requests = 0;
        uint64_t Hits = 0;
        uint64_t HashComputations = 0;
        uint64_t ContentReads = 0;
        uint64_t BytesHashed = 0;
    };

    AssetFileFingerprintCache() = default;
    AssetFileFingerprintCache(const AssetFileFingerprintCache &) = delete;
    AssetFileFingerprintCache & operator=(const AssetFileFingerprintCache &) = delete;

    void Open();
    void Close();
    // 主线程将文件版本与持久化校验记录匹配后填入；Read 仍检查当前原生版本。
    bool Seed(const std::string & path, const AssetFileFingerprint & fingerprint);
    bool Read(const std::string & path, QByteArray * bytes, AssetFileFingerprint & fingerprint,
        std::string & error, bool & changed, Foundation::ITaskContext * context);
    Statistics GetStatistics() const;

private:
    struct Entry {
        AssetFileFingerprint Fingerprint;
        QByteArray Bytes;
        bool HasBytes = false;
        bool Pending = false;
        uint64_t Token = 0;
        std::list<std::string>::iterator Order;
        bool InOrder = false;
    };
    void Trim(uint64_t protectedToken);
    mutable std::mutex _Mutex;
    std::condition_variable _Changed;
    std::map<std::string, Entry> _Entries;
    std::list<std::string> _Order;
    Statistics _Statistics;
    uint64_t _NextToken = 0;
    uint64_t _Epoch = 0;
    uint64_t _RetainedBytes = 0;
    bool _Open = false;
};

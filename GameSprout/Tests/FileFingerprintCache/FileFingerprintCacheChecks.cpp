#include "AssetFileFingerprintCache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Check(bool condition, const char * message) {
    if (!condition) throw std::runtime_error(message);
}

void Write(const QString & path, const QByteArray & bytes) {
    QFile file(path);
    Check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot open temporary fixture");
    Check(file.write(bytes) == bytes.size(), "Cannot write temporary fixture");
    Check(file.flush(), "Cannot flush temporary fixture");
}

void WriteLarge(const QString & path, int mebibytes) {
    QFile file(path);
    Check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot open large fixture");
    const QByteArray block(1024 * 1024, 'L');
    for (int i = 0; i < mebibytes; ++i)
        Check(file.write(block) == block.size(), "Cannot write large fixture");
    Check(file.flush(), "Cannot flush large fixture");
}

std::string Hash(const QByteArray & bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

class Cancellation final : public Foundation::ITaskContext {
public:
    std::atomic<bool> Requested{false};
    UInt64 GetTaskId() const override { return 1; }
    bool IsCancellationRequested() const override { return Requested.load(std::memory_order_acquire); }
    void * GetContext() const override { return nullptr; }
    bool SetOwnedContext(void *, Foundation::fTaskOwnedContextRelease) override { return false; }
};

void CheckConcurrent(const QDir & root) {
    const QString path = root.filePath(QString::fromUtf8("共享纹理_日本語.bin"));
    const QByteArray expected(8 * 1024 * 1024, 'C');
    Write(path, expected);
    AssetFileFingerprintCache cache;
    cache.Open();
    std::atomic<bool> start{false};
    std::array<bool, 8> success{};
    std::array<bool, 8> changed{};
    std::array<std::string, 8> errors;
    std::array<AssetFileFingerprint, 8> fingerprints;
    std::array<std::exception_ptr, 8> failures;
    std::vector<std::thread> workers;
    try {
        for (std::size_t index = 0; index < success.size(); ++index) {
            workers.emplace_back([&, index] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                try {
                    success[index] = cache.Read(path.toStdString(), nullptr, fingerprints[index],
                        errors[index], changed[index], nullptr);
                } catch (...) { failures[index] = std::current_exception(); }
            });
        }
    } catch (...) {
        start.store(true, std::memory_order_release);
        for (std::thread & worker : workers) worker.join();
        throw;
    }
    start.store(true, std::memory_order_release);
    for (std::thread & worker : workers) worker.join();
    const std::string expectedHash = Hash(expected);
    for (std::size_t index = 0; index < success.size(); ++index) {
        if (failures[index]) std::rethrow_exception(failures[index]);
        Check(success[index] && !changed[index] && errors[index].empty(), "A concurrent reader failed");
        Check(fingerprints[index].Hash == expectedHash, "Concurrent reader observed wrong fingerprint");
    }
    const auto statistics = cache.GetStatistics();
    Check(statistics.Requests == 8 && statistics.Hits == 7, "Concurrent requests were not all served");
    Check(statistics.HashComputations == 1 && statistics.ContentReads == 1,
        "Concurrent readers repeated content IO or hash computation");
    Check(statistics.BytesHashed == static_cast<uint64_t>(expected.size()), "Concurrent hash byte count is wrong");
    std::cout << "PASS eight readers share one file read and one hash\n";
}

void CheckRestoredTimestamp(const QDir & root) {
    const QString path = root.filePath("same-stamp.bin");
    const QByteArray original(4096, 'A'), replacement(4096, 'B');
    Write(path, original);
    AssetFileFingerprintCache cache;
    cache.Open();
    AssetFileFingerprint first, second;
    std::string error;
    bool changed = false;
    Check(cache.Read(path.toStdString(), nullptr, first, error, changed, nullptr), "Initial file read failed");
    const std::filesystem::path native = std::filesystem::u8path(path.toStdString());
    const auto modified = std::filesystem::last_write_time(native);
    // QFile truncates this same inode; restoring mtime must not hide new content.
    Write(path, replacement);
    std::filesystem::last_write_time(native, modified);
    QByteArray bytes;
    Check(cache.Read(path.toStdString(), &bytes, second, error, changed, nullptr), "Changed file read failed");
    Check(first.Version.FileID == second.Version.FileID && first.Version.Size == second.Version.Size
        && first.Version.ModifiedNanoseconds == second.Version.ModifiedNanoseconds,
        "Fixture did not preserve identity, size and modification time");
    Check(first.Version.ChangedNanoseconds != second.Version.ChangedNanoseconds,
        "Filesystem did not expose changed metadata time");
    Check(second.Hash == Hash(replacement) && second.Hash != first.Hash && bytes == replacement,
        "Restored modification time hid changed contents");
    Check(cache.GetStatistics().HashComputations == 2 && cache.GetStatistics().ContentReads == 2,
        "A new file version was not rehashed");
    std::cout << "PASS same identity, size and mtime cannot reuse changed contents\n";
}

void CheckPayloadAfterHash(const QDir & root) {
    const QString path = root.filePath("large-payload.bin");
    const QByteArray expected(4 * 1024 * 1024, 'P');
    Write(path, expected);
    AssetFileFingerprintCache cache;
    cache.Open();
    AssetFileFingerprint first, second, third;
    std::string error;
    bool changed = false;
    Check(cache.Read(path.toStdString(), nullptr, first, error, changed, nullptr), "Hash-only read failed");
    QByteArray bytes;
    Check(cache.Read(path.toStdString(), &bytes, second, error, changed, nullptr), "Payload read failed");
    Check(bytes == expected && second.Hash == first.Hash, "Payload and shared fingerprint disagree");
    auto statistics = cache.GetStatistics();
    Check(statistics.ContentReads == 2 && statistics.HashComputations == 1
        && statistics.BytesHashed == static_cast<uint64_t>(expected.size()),
        "Fetching an uncached large payload repeated hashing");
    Check(cache.Read(path.toStdString(), nullptr, third, error, changed, nullptr), "Repeated hash-only read failed");
    statistics = cache.GetStatistics();
    Check(third.Hash == first.Hash && statistics.ContentReads == 2 && statistics.HashComputations == 1,
        "Hash-only lookup reread a known large file");
    std::cout << "PASS large payload fetch reuses hash without retaining every payload\n";
}

void CheckSeed(const QDir & root) {
    const QString path = root.filePath("seeded-payload.bin");
    const QByteArray expected(4 * 1024 * 1024, 'S');
    Write(path, expected);
    AssetFileFingerprint persisted;
    Check(ReadAssetFileStamp(path.toStdString(), persisted.Version), "Cannot inspect seeded fixture");
    persisted.Hash = Hash(expected);
    AssetFileFingerprintCache cache;
    cache.Open();
    Check(cache.Seed(path.toStdString(), persisted), "Valid persisted fingerprint was rejected");
    AssetFileFingerprint actual;
    std::string error;
    bool changed = false;
    Check(cache.Read(path.toStdString(), nullptr, actual, error, changed, nullptr) && actual.Hash == persisted.Hash,
        "Seeded hash lookup failed");
    auto statistics = cache.GetStatistics();
    Check(statistics.HashComputations == 0 && statistics.ContentReads == 0 && statistics.BytesHashed == 0,
        "Seeded hash-only lookup read or rehashed the file");
    QByteArray bytes;
    Check(cache.Read(path.toStdString(), &bytes, actual, error, changed, nullptr) && bytes == expected,
        "Seeded large payload lookup failed");
    statistics = cache.GetStatistics();
    Check(statistics.HashComputations == 0 && statistics.ContentReads == 1 && statistics.BytesHashed == 0,
        "Fetching a seeded payload repeated hashing");

    AssetFileFingerprint invalid = persisted;
    invalid.Hash.pop_back();
    Check(!cache.Seed(path.toStdString(), invalid), "Short hash was accepted");
    invalid.Hash = std::string(64, 'g');
    Check(!cache.Seed(path.toStdString(), invalid), "Non-hex hash was accepted");
    invalid = persisted;
    invalid.Version.Exists = false;
    Check(!cache.Seed(path.toStdString(), invalid), "Missing-file fingerprint was accepted");
    invalid = persisted;
    invalid.Version.Directory = true;
    Check(!cache.Seed(path.toStdString(), invalid), "Directory fingerprint was accepted");
    Check(cache.Read(path.toStdString(), nullptr, actual, error, changed, nullptr) && actual.Hash == persisted.Hash,
        "Rejected seed corrupted the previous valid entry");
    Check(cache.GetStatistics().ContentReads == 1, "Rejected seed discarded a valid hash");

    // A conflicting persisted hash for the same physical version cannot be trusted.
    AssetFileFingerprint conflicting = persisted;
    conflicting.Hash = std::string(64, '0');
    Check(!cache.Seed(path.toStdString(), conflicting), "Conflicting hash for the same version was accepted");
    Check(cache.Read(path.toStdString(), nullptr, actual, error, changed, nullptr) && actual.Hash == persisted.Hash,
        "Conflict fallback failed to recover the real hash");
    statistics = cache.GetStatistics();
    Check(statistics.HashComputations == 1 && statistics.ContentReads == 2,
        "Conflicting seed did not force full verification");
    cache.Close();
    Check(!cache.Seed(path.toStdString(), persisted), "Closed cache accepted a seed");
    std::cout << "PASS persisted seeds avoid IO/hash, payload reads reuse hashes and conflicts fall back\n";
}

void CheckStaleSeed(const QDir & root) {
    const QString path = root.filePath("stale-seed.bin");
    const QByteArray original(2048, 'V'), replacement(2048, 'W');
    Write(path, original);
    AssetFileFingerprint persisted;
    Check(ReadAssetFileStamp(path.toStdString(), persisted.Version), "Cannot inspect stale-seed fixture");
    persisted.Hash = Hash(original);
    AssetFileFingerprintCache cache;
    cache.Open();
    Check(cache.Seed(path.toStdString(), persisted), "Initial seed failed");
    const std::filesystem::path native = std::filesystem::u8path(path.toStdString());
    const auto modified = std::filesystem::last_write_time(native);
    Write(path, replacement);
    std::filesystem::last_write_time(native, modified);
    AssetFileFingerprint actual;
    std::string error;
    bool changed = false;
    Check(cache.Read(path.toStdString(), nullptr, actual, error, changed, nullptr), "New seeded-file version could not be read");
    Check(actual.Version.FileID == persisted.Version.FileID && actual.Version.Size == persisted.Version.Size
        && actual.Version.ModifiedNanoseconds == persisted.Version.ModifiedNanoseconds
        && actual.Version.ChangedNanoseconds != persisted.Version.ChangedNanoseconds,
        "Stale-seed fixture did not preserve file identity, size and mtime");
    Check(actual.Hash == Hash(replacement) && actual.Hash != persisted.Hash
        && cache.GetStatistics().HashComputations == 1 && cache.GetStatistics().ContentReads == 1,
        "Stale persisted seed hid changed file contents");
    std::cout << "PASS seeded hashes still reject a new native file version\n";
}

void CheckSeedEviction(const QDir & root) {
    const QString firstPath = root.filePath("lru-first.bin");
    const QString lastPath = root.filePath("lru-last.bin");
    const QByteArray expected(4096, 'E');
    Write(firstPath, expected);
    Write(lastPath, expected);
    AssetFileFingerprint first, last;
    Check(ReadAssetFileStamp(firstPath.toStdString(), first.Version)
        && ReadAssetFileStamp(lastPath.toStdString(), last.Version), "Cannot inspect LRU fixtures");
    first.Hash = last.Hash = Hash(expected);
    AssetFileFingerprintCache cache;
    cache.Open();
    Check(cache.Seed(firstPath.toStdString(), first), "Cannot seed oldest entry");
    // Seed is intentionally IO-free after the caller has validated its record.
    // Synthetic keys exercise metadata eviction without creating 17000 files.
    for (int index = 0; index < 17000; ++index) {
        const std::string key = root.filePath("lru-key-" + QString::number(index)).toStdString();
        Check(cache.Seed(key, first), "Cannot seed metadata pressure fixture");
    }
    Check(cache.Seed(lastPath.toStdString(), last), "Cannot seed newest entry");
    AssetFileFingerprint actual;
    std::string error;
    bool changed = false;
    Check(cache.Read(lastPath.toStdString(), nullptr, actual, error, changed, nullptr) && actual.Hash == last.Hash,
        "Newest LRU entry was lost");
    Check(cache.GetStatistics().HashComputations == 0 && cache.GetStatistics().ContentReads == 0,
        "Newest seeded entry was reread under metadata pressure");
    Check(cache.Read(firstPath.toStdString(), nullptr, actual, error, changed, nullptr) && actual.Hash == first.Hash,
        "Evicted oldest entry could not be reloaded");
    Check(cache.GetStatistics().HashComputations == 1 && cache.GetStatistics().ContentReads == 1,
        "Oldest entry was not evicted after exceeding the entry limit");

    std::array<QString, 8> paths;
    std::array<bool, 8> success{}, changes{};
    std::array<std::string, 8> errors;
    std::array<AssetFileFingerprint, 8> fingerprints;
    std::array<std::exception_ptr, 8> failures;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        paths[index] = root.filePath("lru-real-" + QString::number(index) + ".bin");
        Write(paths[index], expected);
        AssetFileFingerprint fingerprint;
        Check(ReadAssetFileStamp(paths[index].toStdString(), fingerprint.Version), "Cannot inspect concurrent seeded path");
        fingerprint.Hash = first.Hash;
        Check(cache.Seed(paths[index].toStdString(), fingerprint), "Cannot seed concurrent real path");
    }
    const auto before = cache.GetStatistics();
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    try {
        for (std::size_t index = 0; index < paths.size(); ++index) workers.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            try {
                success[index] = cache.Read(paths[index].toStdString(), nullptr, fingerprints[index],
                    errors[index], changes[index], nullptr);
            } catch (...) { failures[index] = std::current_exception(); }
        });
    } catch (...) {
        start.store(true, std::memory_order_release);
        for (std::thread & worker : workers) worker.join();
        throw;
    }
    start.store(true, std::memory_order_release);
    for (std::thread & worker : workers) worker.join();
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (failures[index]) std::rethrow_exception(failures[index]);
        Check(success[index] && !changes[index] && fingerprints[index].Hash == first.Hash,
            "Concurrent seeded path was corrupted by LRU updates");
    }
    const auto after = cache.GetStatistics();
    Check(after.ContentReads == before.ContentReads && after.HashComputations == before.HashComputations,
        "Seeded paths were rehashed during concurrent LRU hits");
    cache.Close();
    cache.Open();
    Check(cache.Seed(firstPath.toStdString(), first)
        && cache.Read(firstPath.toStdString(), nullptr, actual, error, changed, nullptr),
        "Clearing a populated LRU left invalid state");
    Check(cache.GetStatistics().ContentReads == 0, "Open failed to clear LRU accounting");
    std::cout << "PASS 17000 seeded entries evict safely and concurrent real paths retain valid LRU state\n";
}

void CheckEmptyAndFailures(const QDir & root) {
    const QString path = root.filePath(QString::fromUtf8("空文件.bin"));
    Write(path, {});
    AssetFileFingerprintCache cache;
    cache.Open();
    AssetFileFingerprint fingerprint;
    std::string error;
    bool changed = false;
    Check(cache.Read(path.toStdString(), nullptr, fingerprint, error, changed, nullptr), "Empty file read failed");
    Check(fingerprint.Hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "Empty file hash is wrong");
    QByteArray bytes("stale");
    Check(cache.Read(path.toStdString(), &bytes, fingerprint, error, changed, nullptr) && bytes.isEmpty(),
        "Cached empty payload was treated as absent");
    const auto initial = cache.GetStatistics();
    Check(initial.HashComputations == 1 && initial.ContentReads == 1, "Empty file was repeatedly read");
    const QString missing = root.filePath("not-yet-present.bin");
    for (int attempt = 0; attempt < 2; ++attempt) {
        bytes = "stale";
        Check(!cache.Read(missing.toStdString(), &bytes, fingerprint, error, changed, nullptr)
            && bytes.isEmpty() && fingerprint.Hash.empty() && !error.empty() && !changed,
            "Missing-file error leaked old output");
    }
    Check(cache.GetStatistics().ContentReads == 1, "Missing path was treated as readable contents");
    Write(missing, "arrived");
    Check(cache.Read(missing.toStdString(), &bytes, fingerprint, error, changed, nullptr) && bytes == "arrived",
        "A previous missing-file result poisoned later reads");
    Check(!cache.Read(root.absolutePath().toStdString(), nullptr, fingerprint, error, changed, nullptr),
        "Directory was treated as a file");
    Cancellation cancelled;
    cancelled.Requested.store(true, std::memory_order_release);
    const auto beforeCancel = cache.GetStatistics();
    Check(!cache.Read(path.toStdString(), nullptr, fingerprint, error, changed, &cancelled),
        "Cancelled request unexpectedly succeeded");
    Check(cache.GetStatistics().ContentReads == beforeCancel.ContentReads, "Cancelled request did file IO");
    std::cout << "PASS empty and Unicode paths, missing files and cancellation\n";
}

void CheckCloseAndEpoch(const QDir & root) {
    const QString path = root.filePath("close-epoch.bin");
    Write(path, "epoch contents");
    AssetFileFingerprintCache cache;
    cache.Open();
    AssetFileFingerprint first, second;
    std::string error;
    bool changed = false;
    Check(cache.Read(path.toStdString(), nullptr, first, error, changed, nullptr), "Epoch fixture read failed");
    cache.Close();
    Check(!cache.Read(path.toStdString(), nullptr, second, error, changed, nullptr) && !error.empty(),
        "Closed cache accepted a new request");
    cache.Open();
    Check(cache.GetStatistics().Requests == 0 && cache.GetStatistics().HashComputations == 0,
        "Opening a new epoch retained statistics");
    Check(cache.Read(path.toStdString(), nullptr, second, error, changed, nullptr) && first.Hash == second.Hash,
        "New cache epoch could not read unchanged content");
    Check(cache.GetStatistics().HashComputations == 1 && cache.GetStatistics().ContentReads == 1,
        "New epoch reused an old entry");
    std::cout << "PASS Close rejects requests and Open starts an empty epoch\n";
}

void CheckCloseDuringRead(const QDir & root) {
    const QString path = root.filePath("in-flight.bin");
    WriteLarge(path, 64);
    AssetFileFingerprintCache cache;
    cache.Open();
    bool success = false, changed = false;
    std::string error;
    AssetFileFingerprint fingerprint;
    std::exception_ptr failure;
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        try { success = cache.Read(path.toStdString(), nullptr, fingerprint, error, changed, nullptr); }
        catch (...) { failure = std::current_exception(); }
        finished.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (cache.GetStatistics().ContentReads == 0 && !finished.load(std::memory_order_acquire)
        && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    const bool running = cache.GetStatistics().ContentReads == 1 && !finished.load(std::memory_order_acquire);
    cache.Close();
    // Reset while the old worker is still active. Its result must not enter this epoch.
    cache.Open();
    AssetFileFingerprint current;
    std::string currentError;
    bool currentChanged = false;
    bool currentSuccess = false;
    std::exception_ptr currentFailure;
    try { currentSuccess = cache.Read(path.toStdString(), nullptr, current, currentError, currentChanged, nullptr); }
    catch (...) { currentFailure = std::current_exception(); }
    worker.join();
    if (failure) std::rethrow_exception(failure);
    if (currentFailure) std::rethrow_exception(currentFailure);
    Check(running, "Large fixture finished before an in-flight Close could be exercised");
    Check(!success && changed && !error.empty(), "Closed epoch published an in-flight fingerprint");
    Check(currentSuccess && !currentChanged && current.Version.Size == 64ULL * 1024 * 1024,
        "New epoch was corrupted by an old reader");
    const auto statistics = cache.GetStatistics();
    Check(statistics.Requests == 1 && statistics.ContentReads == 1 && statistics.HashComputations == 1
        && statistics.BytesHashed == 64ULL * 1024 * 1024,
        "Old reader changed statistics or entries in the new epoch");
    std::cout << "PASS in-flight Close/Open isolates the old reader until join\n";
}
}

int main(int argc, char ** argv) {
    QCoreApplication application(argc, argv);
    try {
        QTemporaryDir temporary;
        Check(temporary.isValid(), "Cannot create temporary test project");
        const QDir root(temporary.path());
        CheckConcurrent(root);
        CheckRestoredTimestamp(root);
        CheckPayloadAfterHash(root);
        CheckSeed(root);
        CheckStaleSeed(root);
        CheckSeedEviction(root);
        CheckEmptyAndFailures(root);
        CheckCloseAndEpoch(root);
        CheckCloseDuringRead(root);
        return 0;
    } catch (const std::exception & error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}

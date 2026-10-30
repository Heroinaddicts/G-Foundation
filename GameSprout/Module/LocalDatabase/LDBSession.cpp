#include "LDBSession.h"
#include "LDBTable.h"

#include <QDebug>
#include <algorithm>

namespace {
    // Maps reserve address space; this does not eagerly allocate 256 MiB of RAM.
    constexpr size_t kInitialMapSize = size_t(256) * 1024 * 1024;
    constexpr MDB_dbi kMaxTables = 128;

    std::vector<std::filesystem::path> s_OpenPaths;

    bool ReservePath(const std::filesystem::path & path) {
        for (const auto & active : s_OpenPaths) {
            std::error_code error;
            if (active == path || std::filesystem::equivalent(active, path, error))
                return false;

            // Also recognize aliases created by hard-linking the database file.
            error.clear();
            if (std::filesystem::equivalent(active / "data.mdb", path / "data.mdb", error))
                return false;
        }
        s_OpenPaths.push_back(path);
        return true;
    }

    void ReleasePath(const std::filesystem::path & path) {
        const auto found = std::find(s_OpenPaths.begin(), s_OpenPaths.end(), path);
        if (found != s_OpenPaths.end())
            s_OpenPaths.erase(found);
    }

    bool ReportError(const char * operation, int result) {
        if (result == MDB_SUCCESS)
            return true;
        qWarning("LocalDatabase %s: %s", operation, mdb_strerror(result));
        return false;
    }
}

LDBSession::LDBSession(MDB_env * environment, const std::filesystem::path & path)
    : _Environment(environment), _Path(path),
      _MaxKeySize(static_cast<size_t>(mdb_env_get_maxkeysize(environment))) {}

LDBSession::~LDBSession() {
    Close();
    for (const auto & entry : _Tables)
        delete entry.second;
}

LDBSession * LDBSession::Create(const std::string & path) {
    return OpenInternal(path, true);
}

LDBSession * LDBSession::Open(const std::string & path) {
    return OpenInternal(path, false);
}

LDBSession * LDBSession::OpenInternal(const std::string & path, bool create) {
    if (path.empty() || path.find('\0') != std::string::npos)
        return nullptr;

    std::error_code error;
    const std::u8string utf8Input(path.begin(), path.end());
    std::filesystem::path directory = std::filesystem::absolute(std::filesystem::path(utf8Input), error);
    if (error)
        return nullptr;

    if (create) {
        std::filesystem::create_directories(directory, error);
        if (error)
            return nullptr;
    }
    if (!std::filesystem::is_directory(directory, error) || error)
        return nullptr;

    directory = std::filesystem::canonical(directory, error);
    if (error || !ReservePath(directory))
        return nullptr;

    MDB_env * environment = nullptr;
    try {
        const std::filesystem::path databaseFile = directory / "data.mdb";
        const std::filesystem::file_status status = std::filesystem::symlink_status(databaseFile, error);
        if (error == std::errc::no_such_file_or_directory)
            error.clear();
        const bool exists = status.type() != std::filesystem::file_type::not_found;
        if (error || (create && exists) ||
            (!create && (!exists || !std::filesystem::is_regular_file(databaseFile, error))) || error) {
            ReleasePath(directory);
            return nullptr;
        }
        // LMDB initializes zero-byte files; Open must reject them rather than create a new DB.
        if (!create && (std::filesystem::file_size(databaseFile, error) == 0 || error)) {
            ReleasePath(directory);
            return nullptr;
        }

        int result = mdb_env_create(&environment);
        if (result == MDB_SUCCESS)
            result = mdb_env_set_maxdbs(environment, kMaxTables);
        if (result == MDB_SUCCESS && create)
            result = mdb_env_set_mapsize(environment, kInitialMapSize);
        if (result == MDB_SUCCESS) {
            const auto utf8Path = directory.u8string();
            result = mdb_env_open(environment,
                reinterpret_cast<const char *>(utf8Path.c_str()), 0, 0644);
        }
        if (!ReportError(create ? "create" : "open", result)) {
            if (environment)
                mdb_env_close(environment);
            ReleasePath(directory);
            return nullptr;
        }
        return new LDBSession(environment, directory);
    } catch (...) {
        if (environment)
            mdb_env_close(environment);
        ReleasePath(directory);
        throw;
    }
}

bool LDBSession::CreateTable(const std::string & name) {
    return OpenTable(name, true) != nullptr;
}

ILDBTable * LDBSession::GetTable(const std::string & name) {
    return OpenTable(name, false);
}

LDBTable * LDBSession::OpenTable(const std::string & name, bool create) {
    if (!_Environment || !IsValidKey(name) || name.find('\0') != std::string::npos)
        return nullptr;

    const auto found = _Tables.find(name);
    if (found != _Tables.end())
        return found->second;

    MDB_txn * transaction = nullptr;
    int result = mdb_txn_begin(_Environment, nullptr, 0, &transaction);
    if (!ReportError("begin table transaction", result))
        return nullptr;

    MDB_dbi database = 0;
    result = mdb_dbi_open(transaction, name.c_str(), create ? MDB_CREATE : 0, &database);
    if (result != MDB_SUCCESS) {
        mdb_txn_abort(transaction);
        if (result != MDB_NOTFOUND)
            ReportError("open table", result);
        return nullptr;
    }

    unsigned int flags = 0;
    result = mdb_dbi_flags(transaction, database, &flags);
    if (result != MDB_SUCCESS || (flags & (MDB_INTEGERKEY | MDB_DUPSORT))) {
        mdb_txn_abort(transaction);
        if (result != MDB_SUCCESS)
            ReportError("read table flags", result);
        else
            qWarning("LocalDatabase cannot open an integer-key or duplicate-value table through this interface");
        return nullptr;
    }

    // Opening a DBI must be committed before the handle is reused by other transactions.
    result = mdb_txn_commit(transaction);
    if (!ReportError("commit table transaction", result))
        return nullptr;

    LDBTable * table = nullptr;
    try {
        table = new LDBTable(this, database);
        _Tables.emplace(name, table);
    } catch (...) {
        delete table;
        mdb_dbi_close(_Environment, database);
        throw;
    }
    return table;
}

bool LDBSession::IsValidKey(const std::string & key) const {
    return !key.empty() && key.size() <= _MaxKeySize;
}

bool LDBSession::WriteBatch(const std::vector<LDBWriteOperation> & operations) {
    if (!_Environment) return false;
    if (operations.empty()) return true;
    // Resolve handles before opening the write transaction (LMDB allows one writer).
    std::vector<MDB_dbi> databases;
    databases.reserve(operations.size());
    for (const LDBWriteOperation & operation : operations) {
        if (!IsValidKey(operation.Key)) return false;
        LDBTable * table = OpenTable(operation.Table, false);
        if (!table) return false;
        databases.push_back(table->_Database);
    }
    MDB_txn * transaction = nullptr;
    int result = mdb_txn_begin(_Environment, nullptr, 0, &transaction);
    if (!ReportError("begin batch", result)) return false;
    uint8_t empty = 0;
    for (size_t i = 0; i < operations.size(); ++i) {
        const LDBWriteOperation & operation = operations[i];
        MDB_val key{operation.Key.size(), const_cast<char *>(operation.Key.data())};
        if (operation.Delete) {
            result = mdb_del(transaction, databases[i], &key, nullptr);
            if (result == MDB_NOTFOUND) result = MDB_SUCCESS;
        } else {
            MDB_val value{operation.Data.size(), operation.Data.empty()
                ? &empty : const_cast<uint8_t *>(operation.Data.data())};
            result = mdb_put(transaction, databases[i], &key, &value, 0);
        }
        if (result != MDB_SUCCESS) {
            mdb_txn_abort(transaction);
            return ReportError("write batch", result);
        }
    }
    return ReportError("commit batch", mdb_txn_commit(transaction));
}

void LDBSession::Close() {
    if (!_Environment)
        return;

    mdb_env_close(_Environment);
    _Environment = nullptr;
    ReleasePath(_Path);
    // Wrappers stay alive until this session is destroyed; they now reject operations.
}

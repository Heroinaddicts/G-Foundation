#include "LDBTable.hpp"
#include "LDBSession.hpp"

#include <QDebug>

namespace {
bool ReportError(const char * operation, int result) {
    if (result == MDB_SUCCESS)
        return true;
    qWarning("LocalDatabase %s: %s", operation, mdb_strerror(result));
    return false;
}
}

bool LDBTable::IsExists(const std::string & key) const {
    return Read(key, nullptr);
}

bool LDBTable::Get(const std::string & key, std::vector<uint8_t> & data) const {
    data.clear();
    return Read(key, &data);
}

bool LDBTable::Read(const std::string & key, std::vector<uint8_t> * data) const {
    if (!_Owner._Environment || !_Owner.IsValidKey(key))
        return false;

    MDB_txn * transaction = nullptr;
    int result = mdb_txn_begin(_Owner._Environment, nullptr, MDB_RDONLY, &transaction);
    if (!ReportError("begin read transaction", result))
        return false;

    MDB_val nativeKey{key.size(), const_cast<char *>(key.data())};
    MDB_val value{};
    result = mdb_get(transaction, _Database, &nativeKey, &value);
    if (result != MDB_SUCCESS) {
        mdb_txn_abort(transaction);
        if (result != MDB_NOTFOUND)
            ReportError("get", result);
        return false;
    }

    try {
        if (data && value.mv_size > 0) {
            const auto * begin = static_cast<const uint8_t *>(value.mv_data);
            data->assign(begin, begin + value.mv_size);
        }
    } catch (...) {
        mdb_txn_abort(transaction);
        throw;
    }
    mdb_txn_abort(transaction);
    return true;
}

bool LDBTable::Set(const std::string & key, void * const data, const int len) {
    if (len < 0 || (len > 0 && !data))
        return false;

    if (!_Owner._Environment || !_Owner.IsValidKey(key))
        return false;

    MDB_txn * transaction = nullptr;
    int result = mdb_txn_begin(_Owner._Environment, nullptr, 0, &transaction);
    if (!ReportError("begin write transaction", result))
        return false;

    MDB_val nativeKey{key.size(), const_cast<char *>(key.data())};
    // LMDB can call memcpy even for a zero-length value; provide a valid address.
    uint8_t empty = 0;
    MDB_val value{static_cast<size_t>(len), data ? data : &empty};
    result = mdb_put(transaction, _Database, &nativeKey, &value, 0);
    if (result != MDB_SUCCESS) {
        mdb_txn_abort(transaction);
        return ReportError("set", result);
    }
    return ReportError("commit write transaction", mdb_txn_commit(transaction));
}

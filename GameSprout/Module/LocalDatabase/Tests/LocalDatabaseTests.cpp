#include "LocalDatabase.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;
using Session = std::unique_ptr<ILDBSession>;

void Check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

// Only a directory successfully created by this instance is ever removed.
class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        std::random_device random;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 128; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                ("gamesprout-localdatabase-tests-" + std::to_string(stamp) + "-" +
                 std::to_string(random()) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                _Path = std::move(candidate);
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "Cannot create test directory", candidate, error);
        }
        throw std::runtime_error("Cannot allocate a unique test directory");
    }

    ~TemporaryDirectory() {
        if (!_Path.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(_Path, ignored);
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& Path() const { return _Path; }

private:
    std::filesystem::path _Path;
};

void Put(ILDBTable& table, const std::string& key, Bytes value) {
    Check(table.Set(key, value.empty() ? nullptr : value.data(),
                    static_cast<int>(value.size())),
          "Set failed for a valid key/value");
}

void ExpectValue(const ILDBTable& table, const std::string& key, const Bytes& expected) {
    Bytes actual{0xEE};
    Check(table.Get(key, actual), "Get failed for an existing key");
    Check(actual == expected, "Get returned incorrect value bytes");
    Check(table.IsExists(key), "IsExists rejected an existing key");
}

ILDBTable& RequireTable(ILDBSession& session, const std::string& name) {
    Check(session.CreateTable(name), "CreateTable failed");
    ILDBTable* table = session.GetTable(name);
    Check(table != nullptr, "GetTable failed after CreateTable");
    return *table;
}

void TestCreateAndOpen(ILocalDatabase& database, const std::filesystem::path& path) {
    Check(database.CreateLDB("") == nullptr && database.OpenLDB("") == nullptr,
          "Empty database paths must be rejected");
    const auto emptyDatabase = path.parent_path() / "empty-database";
    std::filesystem::create_directory(emptyDatabase);
    std::ofstream(emptyDatabase / "data.mdb", std::ios::binary).close();
    Check(database.OpenLDB(emptyDatabase.string()) == nullptr,
          "OpenLDB must not initialize an existing empty file");
    Check(std::filesystem::file_size(emptyDatabase / "data.mdb") == 0,
          "OpenLDB modified an invalid empty database");

    const auto missing = path / "missing";
    Check(database.OpenLDB(missing.string()) == nullptr,
          "OpenLDB must reject a missing database");
    Check(!std::filesystem::exists(missing),
          "OpenLDB must not create a missing directory");

    Session session(database.CreateLDB(path.string()));
    Check(session != nullptr, "CreateLDB failed for a new database");
    Check(database.CreateLDB(path.string()) == nullptr,
          "CreateLDB must reject an active database");
    Check(database.OpenLDB(path.string()) == nullptr,
          "OpenLDB must reject an already active environment");

    std::unique_ptr<ILocalDatabase> other(new LocalDatabase());
    Check(other->OpenLDB(path.string()) == nullptr,
          "Active environment exclusion must apply across LocalDatabase instances");

    const auto absolute = std::filesystem::absolute(path);
    const auto relative = std::filesystem::relative(absolute, std::filesystem::current_path());
    Check(!relative.empty() && relative.is_relative(), "Cannot form a relative test path");
    Check(database.OpenLDB(relative.string()) == nullptr,
          "Relative and absolute paths must identify the same active environment");
    Check(database.CreateLDB(relative.string()) == nullptr,
          "CreateLDB must reject an alias of an active environment");

    Check(session->GetTable("missing-table") == nullptr,
          "GetTable must not create missing tables");
    Check(session->CreateTable("existing-table"), "CreateTable failed");
    Check(session->CreateTable("existing-table"),
          "Creating an existing table must succeed idempotently");
    Check(!session->CreateTable("") && session->GetTable("") == nullptr,
          "Empty table names must be rejected");
    const std::string invalidName("table\0suffix", 12);
    Check(!session->CreateTable(invalidName) && session->GetTable(invalidName) == nullptr,
          "Embedded NULs in table names must be rejected");

    session->Close();
    Check(database.CreateLDB(path.string()) == nullptr,
          "CreateLDB must reject an existing database after close");
    Session reopened(database.OpenLDB(relative.string()));
    Check(reopened != nullptr, "OpenLDB failed for a closed existing database");
    Check(reopened->GetTable("missing-table") == nullptr,
          "A previous GetTable lookup must not have persisted a new table");
    Check(reopened->GetTable("existing-table") != nullptr,
          "An empty named table must persist across reopen");
}

void TestValuesAndIsolation(ILocalDatabase& database, const std::filesystem::path& path) {
    Session session(database.CreateLDB(path.string()));
    Check(session != nullptr, "Cannot create value test database");
    ILDBTable& textures = RequireTable(*session, "textures");
    ILDBTable& meshes = RequireTable(*session, "meshes");

    const std::string binaryKey("asset\0key", 9);
    const Bytes binaryValue{0x00, 0x01, 0x00, 0x7F, 0xFF, 0x00};
    const Bytes otherValue{0xFF, 0x00, 0x02};
    Put(textures, binaryKey, binaryValue);
    Put(meshes, binaryKey, otherValue);
    ExpectValue(textures, binaryKey, binaryValue);
    ExpectValue(meshes, binaryKey, otherValue);
    Put(textures, "texture-only", {0x42});
    Check(!meshes.IsExists("texture-only"), "Keys leaked between tables");

    Check(textures.Set("empty", nullptr, 0), "A null, zero-length value must be valid");
    ExpectValue(textures, "empty", {});
    ExpectValue(textures, binaryKey, binaryValue);

    // A shorter replacement must not retain bytes from the old value.
    Put(textures, binaryKey, {0x03, 0x00});
    ExpectValue(textures, binaryKey, {0x03, 0x00});
    ExpectValue(meshes, binaryKey, otherValue);

    Bytes output{0x01, 0x02};
    Check(!textures.Get("missing", output), "Get must reject a missing key");
    Check(output.empty(), "A failed Get must clear caller-owned output");
    Check(!textures.IsExists("missing"), "IsExists accepted a missing key");

    Bytes data{0x10, 0x20};
    Check(!textures.Set("", data.data(), static_cast<int>(data.size())),
          "Set must reject an empty key");
    Check(!textures.Set("negative", data.data(), -1),
          "Set must reject a negative length");
    Check(!textures.Set("null-positive", nullptr, 1),
          "Set must reject a null value with nonzero length");
    Check(!textures.IsExists("negative") && !textures.IsExists("null-positive"),
          "Invalid writes must not create keys");
    Check(!textures.IsExists(""), "IsExists must reject an empty key");
    const std::string oversizedKey(10000, 'x');
    Check(!textures.Set(oversizedKey, data.data(), static_cast<int>(data.size())) &&
          !textures.IsExists(oversizedKey), "Oversized keys must be rejected");
    output = {0x77};
    Check(!textures.Get("", output) && output.empty(),
          "Get of an empty key must fail and clear output");

    Put(textures, "stable", {0x30, 0x40});
    Check(!textures.Set("stable", nullptr, 2), "An invalid overwrite must fail");
    Check(!textures.Set("stable", data.data(), -1), "A negative overwrite must fail");
    ExpectValue(textures, "stable", {0x30, 0x40});
    Check(textures.Set("stable", nullptr, 0), "An empty overwrite must succeed");
    ExpectValue(textures, "stable", {});
}

void TestPersistenceAndClose(ILocalDatabase& database, const std::filesystem::path& path) {
    const std::string key("persist\0key", 11);
    const Bytes value{0x00, 0x09, 0x00, 0xFF};
    Session session(database.CreateLDB(path.string()));
    Check(session != nullptr, "Cannot create persistence test database");
    ILDBTable* oldTable = &RequireTable(*session, "primary");
    ILDBTable& secondary = RequireTable(*session, "secondary");
    Put(*oldTable, key, value);
    Put(*oldTable, "empty", {});
    Put(secondary, key, {0xA5});

    session->Close();
    session->Close();
    // Borrowed wrappers may only be used while their owning session still lives.
    Bytes output{0x99};
    uint8_t byte = 1;
    Check(!oldTable->Set("closed", &byte, 1), "A closed table must reject Set");
    Check(!oldTable->Get(key, output) && output.empty(),
          "A closed table must reject Get and clear output");
    Check(!oldTable->IsExists(key), "A closed table must reject IsExists");
    Check(!session->CreateTable("closed"), "A closed session must reject CreateTable");
    Check(session->GetTable("primary") == nullptr,
          "A closed session must reject GetTable");

    // Close must release the native environment even before the wrapper is deleted.
    Session reopened(database.OpenLDB(path.string()));
    Check(reopened != nullptr, "Close did not release the active environment");
    session->Close(); // An old session must not release the new session's reservation.
    Check(database.OpenLDB(path.string()) == nullptr,
          "Closing an old session released a different active session");
    session.reset();
    Check(database.OpenLDB(path.string()) == nullptr,
          "Deleting an old closed session released a different active session");
    ILDBTable* primary = reopened->GetTable("primary");
    ILDBTable* other = reopened->GetTable("secondary");
    Check(primary != nullptr && other != nullptr, "Named tables did not persist");
    ExpectValue(*primary, key, value);
    ExpectValue(*primary, "empty", {});
    ExpectValue(*other, key, {0xA5});
    reopened.reset(); // Session destruction must automatically close the database.

    Session afterDestruction(database.OpenLDB(path.string()));
    Check(afterDestruction != nullptr, "Session destruction did not close the environment");
    ILDBTable* persisted = afterDestruction->GetTable("primary");
    Check(persisted != nullptr, "Table was lost after session deletion");
    ExpectValue(*persisted, key, value);
}

void TestAutomaticClose(ILocalDatabase& database, const std::filesystem::path& path) {
    {
        Session session(database.CreateLDB(path.string()));
        Check(session != nullptr, "Cannot create automatic-close test database");
        Put(RequireTable(*session, "records"), "key", {0x12, 0x00, 0x34});
        // No explicit Close call in this scope.
    }
    Session reopened(database.OpenLDB(path.string()));
    Check(reopened != nullptr, "Destructor failed to close an open session");
    ILDBTable* records = reopened->GetTable("records");
    Check(records != nullptr, "Destructor-close did not persist table data");
    ExpectValue(*records, "key", {0x12, 0x00, 0x34});
}

void TestIteration(ILocalDatabase& database, const std::filesystem::path& path) {
    ILDBSession* session = database.CreateLDB(path.string());
    Check(session != nullptr, "Cannot create iteration test database");
    try {
        ILDBTable& table = RequireTable(*session, "records");
        int calls = 0;
        Check(table.ForEach([&](const std::string&, const void*, int) {
            ++calls;
            return true;
        }) && calls == 0, "Empty table iteration must succeed without callbacks");
        Check(!table.ForEach({}), "An empty iteration callback must be rejected");

        const std::string binaryKey("asset\0key", 9);
        const std::map<std::string, Bytes> expected{
            {"empty", {}}, {binaryKey, {0x00, 0x7F, 0xFF, 0x00}},
            {"ordinary", {0x12, 0x34}}
        };
        for (const auto& entry : expected)
            Put(table, entry.first, entry.second);
        Put(RequireTable(*session, "other"), "other-table-only", {0x55});

        std::map<std::string, Bytes> visited;
        const auto caller = std::this_thread::get_id();
        Check(table.ForEach([&](const std::string& key, const void* data, int len) {
            Check(std::this_thread::get_id() == caller, "Iteration callback changed threads");
            Check(len >= 0 && (len == 0 || data != nullptr), "Invalid iteration buffer");
            Bytes copy;
            if (len > 0) {
                const auto* begin = static_cast<const uint8_t*>(data);
                copy.assign(begin, begin + len);
            }
            Check(visited.emplace(key, std::move(copy)).second, "Iteration duplicated a key");
            return true;
        }), "Complete iteration failed");
        Check(visited == expected, "Iteration lost binary data or crossed table boundaries");

        for (int round = 0; round < 8; ++round) {
            calls = 0;
            Check(table.ForEach([&](const std::string&, const void*, int) {
                ++calls;
                return false;
            }) && calls == 1, "Early termination must succeed after one callback");

            bool caught = false;
            try {
                table.ForEach([](const std::string&, const void*, int) -> bool {
                    throw std::runtime_error("iteration callback failed");
                });
            } catch (const std::runtime_error& error) {
                caught = std::string(error.what()) == "iteration callback failed";
            }
            Check(caught, "Iteration swallowed the callback exception");
            calls = 0;
            Check(table.ForEach([&](const std::string&, const void*, int) {
                ++calls;
                return true;
            }) && calls == static_cast<int>(expected.size()),
                "Early stop or exception leaked the read transaction");
        }
        Put(table, "after-iteration", {0xA5});
        ExpectValue(table, "after-iteration", {0xA5});
        for (const auto& entry : expected)
            ExpectValue(table, entry.first, entry.second);

        session->Close();
        calls = 0;
        Check(!table.ForEach([&](const std::string&, const void*, int) {
            ++calls;
            return true;
        }) && calls == 0, "Closed session must reject iteration without callbacks");
    } catch (...) {
        delete session;
        throw;
    }
    delete session;
}

} // namespace

int main() {
    try {
        TemporaryDirectory temporary;
        std::unique_ptr<ILocalDatabase> database(new LocalDatabase());
        TestCreateAndOpen(*database, temporary.Path() / "create-open");
        std::cout << "[PASS] Create/open rules, aliases, and named tables\n";
        TestValuesAndIsolation(*database, temporary.Path() / "values");
        std::cout << "[PASS] Binary values, table isolation, and invalid input\n";
        TestPersistenceAndClose(*database, temporary.Path() / "persistence");
        std::cout << "[PASS] Persistence, idempotent Close, and closed wrappers\n";
        TestAutomaticClose(*database, temporary.Path() / "automatic-close");
        std::cout << "[PASS] Session destructor closes and persists\n";
        TestIteration(*database, temporary.Path() / "iteration");
        std::cout << "[PASS] Iteration, early stop, binary data, callback exceptions and cleanup\n";
        std::cout << "All LocalDatabase tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}

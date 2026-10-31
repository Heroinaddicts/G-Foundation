#include "AssetTestSupport.h"
#include <chrono>

struct DatabaseActivity {
    const std::thread::id Main = std::this_thread::get_id();
    int WriteBatches = 0;
    int SourceWrites = 0;
    int PendingWrites = 0;
};

// Observe actual synchronous writes without exposing production-only counters.
class CountingSession final : public ILDBSession {
public:
    CountingSession(ILDBSession * session, DatabaseActivity & activity) : _Session(session), _Activity(activity) {}
    ~CountingSession() override { delete _Session; }
    bool CreateTable(const std::string & name) override { CheckThread(); return _Session->CreateTable(name); }
    ILDBTable * GetTable(const std::string & name) override { CheckThread(); return _Session->GetTable(name); }
    bool WriteBatch(const std::vector<LDBWriteOperation> & operations) override {
        CheckThread();
        if (!operations.empty()) ++_Activity.WriteBatches;
        for (const auto & operation : operations) if (operation.Table == "AssetSources") {
            ++_Activity.SourceWrites;
            if (!operation.Delete) {
                Json::Value value;
                Check(AssetArtifactIO::Parse(std::string(operation.Data.begin(), operation.Data.end()), value), "Invalid source write");
                if (value["Status"] == "Pending") ++_Activity.PendingWrites;
            }
        }
        return _Session->WriteBatch(operations);
    }
    void Close() override { CheckThread(); _Session->Close(); }
private:
    void CheckThread() const { Check(std::this_thread::get_id() == _Activity.Main, "Database accessed from worker"); }
    ILDBSession * const _Session;
    DatabaseActivity & _Activity;
};

class CountingDatabase final : public ILocalDatabase {
public:
    explicit CountingDatabase(ILocalDatabase * database) : _Database(database) {}
    DatabaseActivity Activity;
    ILDBSession * CreateLDB(const std::string & path) override { return Wrap(_Database->CreateLDB(path)); }
    ILDBSession * OpenLDB(const std::string & path) override { return Wrap(_Database->OpenLDB(path)); }
    void BeforeInitialize(GameSproutEditor::GameSproutModuleApi * api) override {
        GameSproutEditor::GameSproutEditorFinal::Before(_Database, api);
    }
    bool Initialize(GameSproutEditor::GameSproutModuleApi * api) override {
        return GameSproutEditor::GameSproutEditorFinal::Initialize(_Database, api);
    }
    bool Launch(GameSproutEditor::GameSproutModuleApi * api) override {
        return GameSproutEditor::GameSproutEditorFinal::Launch(_Database, api);
    }
    void Release(GameSproutEditor::GameSproutModuleApi * api) override {
        GameSproutEditor::GameSproutEditorFinal::Release(_Database, api);
    }
private:
    ILDBSession * Wrap(ILDBSession * session) {
        Check(std::this_thread::get_id() == Activity.Main, "Database opened from worker");
        return session ? new CountingSession(session, Activity) : nullptr;
    }
    ILocalDatabase * const _Database;
};

Json::Value ReadJson(const QString & path) {
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly), "Cannot read test data: " + path.toStdString());
    Json::Value value;
    Check(AssetArtifactIO::Parse(file.readAll().toStdString(), value), "Invalid test JSON");
    return value;
}

void WriteJson(const QString & path, const Json::Value & value) {
    std::string error;
    Check(AssetArtifactIO::Write(path, QByteArray::fromStdString(AssetArtifactIO::JsonText(value)), error), error);
}

Json::Value Snapshot(ILibraryManager * library) {
    Json::Value state;
    auto * table = library->GetLDBSession()->GetTable("AssetSources");
    Check(table && table->ForEach([&](const std::string & guid, const void * bytes, int length) {
        Json::Value value;
        Check(AssetArtifactIO::Parse(std::string(static_cast<const char *>(bytes), size_t(length)), value), "Invalid source row");
        state["Sources"][guid] = value;
        state["Paths"][value["Path"].asString()] = guid;
        return true;
    }), "Cannot enumerate sources");
    for (const std::string & guid : library->GetManifestGuids()) {
        AssetImportManifest manifest;
        Check(library->GetManifest(Guid(guid), manifest), "Invalid manifest");
        state["Manifests"][guid] = AssetArtifactIO::ToJson(manifest);
        std::string validation;
        if (library->GetImportValidation(Guid(guid), validation)) state["ValidationJson"][guid] = validation;
    }
    return state;
}

int RunEditor(const QString & projectRoot, int phase, int argc, char ** argv, int expectedSources = -1) {
    auto * real = Foundation::GetFoundationApi(TEST_BINARY_ROOT);
    Check(real && real->Initialize(argc, argv) && real->Launch(), "Foundation startup failed");
    TestIDE ide;
    TestProject project;
    TestConsole console;
    TestEditor editor;
    project.Root = projectRoot.toStdString();
    editor.IDE = &ide;
    editor.Modules["Project"] = &project;
    editor.Modules["Console"] = &console;
    FoundationProxy foundation;
    foundation.Real = real;
    foundation.Tasks.Real = real->TaskApi();
    foundation.Tasks.IDE = &ide;
    GameSproutEditor::GameSproutModuleApi api(&foundation, &editor);
    CountingDatabase database(static_cast<ILocalDatabase *>(LoadModule("LocalDatabase")));
    editor.Modules["LocalDatabase"] = &database;
    for (const std::string & name : {"LibraryManager", "ShaderCompiler", "AssetsPipeline", "AssetsManager", "GuidCreator"})
        editor.Modules[name] = LoadModule(name);
    for (const auto & entry : editor.Modules) GameSproutEditor::GameSproutEditorFinal::Before(entry.second, &api);
    for (const auto & entry : editor.Modules)
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second, &api), "Initialize failed: " + entry.first);
    for (const auto & entry : editor.Modules) if (entry.first != "Project")
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second, &api), "Launch failed: " + entry.first);
    Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project, &api), "Project launch failed");
    auto * manager = static_cast<IAssetsManager *>(editor.Modules["AssetsManager"]);
    auto * library = static_cast<ILibraryManager *>(editor.Modules["LibraryManager"]);
    auto * pipeline = static_cast<IAssetsPipeline *>(editor.Modules["AssetsPipeline"]);
    const auto started = std::chrono::steady_clock::now();
    GameSproutEditor::GameSproutEditorFinal::Finished(manager, &api);
    const auto scanMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    GameSproutEditor::GameSproutEditorFinal::Finished(library, &api);
    const int initialLoads = ide.Loads;
    const auto waitForImports = [&] {
        for (;;) {
            real->Update();
            QCoreApplication::processEvents();
            GameSproutEditor::GameSproutEditorFinal::Update(manager, &api);
            if (ide.Count == 0) break;
            Check(ide.Count > 0, "Startup Loading became invalid");
            Check(std::chrono::steady_clock::now() - started < std::chrono::seconds(expectedSources < 0 ? 35 : 180), "Warm startup timed out");
            QThread::msleep(1);
        }
    };
    waitForImports();
    Check(ide.Loads == initialLoads, "Startup submitted another import after ScanDirectory returned");
    if (phase == 14 && expectedSources < 0) {
        IAssetMeta * target = nullptr;
        for (IAssetMeta * meta : manager->GetAssetMetas()) if (meta->GetPath() == "Stable.txt") target = meta;
        Check(target && manager->ReimportAsset(target->_Guid), "Explicit reimport was rejected");
        waitForImports();
    }
    Check(console.Errors.empty(), "Unexpected import error");
    Json::Value state = Snapshot(library);
    Check(state["Sources"].size() == (expectedSources < 0 ? (phase == 1 ? 5 : 6) : expectedSources), "Unexpected source inventory size");
    for (const auto & source : state["Sources"])
        Check(source["Status"] == "Ready" || source["Status"] == "NotRequired", "Startup retained a pending/failed source");
    if (phase == 3 && expectedSources < 0) {
        const std::string guid = state["Paths"]["Main.png"].asString();
        const std::string generation = state["Manifests"][guid]["Generation"].asString();
        std::string validation;
        Check(library->GetImportValidation(Guid(guid), validation), "Successful import did not persist fast-validation data");
        const int batches = database.Activity.WriteBatches;
        Check(library->SaveImportValidation(Guid(guid), generation, validation), "Identical validation save failed");
        Check(database.Activity.WriteBatches == batches, "Identical validation save wrote a redundant transaction");
        Check(!library->SaveImportValidation(Guid(guid), generation, "{invalid"), "Malformed validation was accepted");
        Json::Value stale;
        Check(AssetArtifactIO::Parse(validation, stale), "Invalid saved validation JSON");
        stale["Generation"] = generation + "-stale";
        Check(!library->SaveImportValidation(Guid(guid), generation + "-stale", AssetArtifactIO::JsonText(stale)), "Stale generation validation was accepted");
        std::string retained;
        Check(library->GetImportValidation(Guid(guid), retained) && retained == validation
            && database.Activity.WriteBatches == batches, "Rejected validation changed the active cache");
    }
    state["Activity"]["Tasks"] = foundation.Tasks.Submissions;
    state["Activity"]["ResourceLoads"] = ide.ResourceLoads;
    state["Activity"]["SourceWrites"] = database.Activity.SourceWrites;
    state["Activity"]["PendingWrites"] = database.Activity.PendingWrites;
    state["Activity"]["WriteBatches"] = database.Activity.WriteBatches;
    state["Activity"]["StartupScanMilliseconds"] = Json::Int64(scanMilliseconds);
    state["Activity"]["Milliseconds"] = Json::Int64(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    WriteJson(QDir(projectRoot).filePath("phase-" + QString::number(phase) + ".json"), state);
    if (phase == 10 && expectedSources < 0) {
        // Change the successful importer version while the editor owns this DB.
        // The next independent launch must reject that otherwise intact cache.
        const std::string guid = state["Paths"]["Other.png"].asString();
        Json::Value manifest = state["Manifests"][guid];
        manifest["ImporterVersion"] = Json::UInt(manifest["ImporterVersion"].asUInt() + 1);
        const std::string json = AssetArtifactIO::JsonText(manifest);
        Check(library->GetLDBSession()->GetTable("Manifests")->Set(guid, const_cast<char *>(json.data()), int(json.size())), "Cannot change importer-version fixture");
    }
    if (phase == 15 && expectedSources < 0) {
        const std::string guid = state["Paths"]["Other.png"].asString();
        Json::Value manifest = state["Manifests"][guid];
        manifest.removeMember("Generation");
        const std::string json = AssetArtifactIO::JsonText(manifest);
        Check(library->GetLDBSession()->GetTable("Manifests")->Set(guid, const_cast<char *>(json.data()), int(json.size())),
            "Cannot damage cached manifest fixture");
    }
    GameSproutEditor::GameSproutEditorFinal::Release(manager, &api);
    GameSproutEditor::GameSproutEditorFinal::Release(pipeline, &api);
    GameSproutEditor::GameSproutEditorFinal::Release(library, &api);
    Check(ide.Count == 0 && ide.Loads == ide.Unloads, "Loading imbalance");
    real->Release();
    return 0;
}

Json::Value StartEditor(const QDir & root, int phase, int expectedSources = -1) {
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    QStringList arguments{expectedSources < 0 ? "--child" : "--benchmark-child", root.absolutePath(), QString::number(phase)};
    if (expectedSources >= 0) arguments.push_back(QString::number(expectedSources));
    process.setArguments(arguments);
    process.start();
    Check(process.waitForStarted(5000) && process.waitForFinished(expectedSources < 0 ? 45000 : 200000), "Editor subprocess timed out");
    const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
    Check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "Phase " + std::to_string(phase) + " failed: " + output.toStdString());
    const Json::Value state = ReadJson(root.filePath("phase-" + QString::number(phase) + ".json"));
    std::cout << "phase " << phase << ": imports=" << state["Activity"]["ResourceLoads"].asInt()
        << ", source writes=" << state["Activity"]["SourceWrites"].asInt()
        << ", pending=" << state["Activity"]["PendingWrites"].asInt()
        << ", write batches=" << state["Activity"]["WriteBatches"].asInt()
        << ", scan=" << state["Activity"]["StartupScanMilliseconds"].asInt64() << "ms"
        << ", elapsed=" << state["Activity"]["Milliseconds"].asInt64() << "ms\n";
    return state;
}

const Json::Value & Manifest(const Json::Value & state, const std::string & path) {
    return state["Manifests"][state["Paths"][path].asString()];
}

void CheckWarm(const Json::Value & before, const Json::Value & after) {
    Check(after["Activity"]["ResourceLoads"] == 0, "Unchanged startup submitted resource tasks");
    Check(after["Activity"]["SourceWrites"] == 0 && after["Activity"]["PendingWrites"] == 0, "Unchanged startup rewrote source status");
    Check(after["Activity"]["WriteBatches"] == 0, "Unchanged startup submitted database writes");
    Check(before["Sources"] == after["Sources"] && before["Manifests"] == after["Manifests"]
        && before["ValidationJson"] == after["ValidationJson"], "Unchanged startup rewrote cache data");
}

void OverwriteSameStamp(const QString & path, const QByteArray & bytes) {
    const std::filesystem::path native(path.toStdU16String());
    const auto timestamp = std::filesystem::last_write_time(native);
    Check(QFileInfo(path).size() == bytes.size(), "Same-length fixture has wrong size");
    // Keep the same file identity as well as its size and restored mtime: only
    // the operating system's change-time/version observation exposes this edit.
    QFile file(path);
    Check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot overwrite same-stamp fixture");
    Check(file.write(bytes) == bytes.size() && file.flush(), "Cannot write same-stamp fixture");
    file.close();
    std::filesystem::last_write_time(native, timestamp);
}

int main(int argc, char ** argv) {
    QGuiApplication application(argc, argv);
    try {
        if (argc == 4 && std::string(argv[1]) == "--child") return RunEditor(QString::fromUtf8(argv[2]), std::stoi(argv[3]), argc, argv);
        if (argc == 5 && std::string(argv[1]) == "--benchmark-child")
            return RunEditor(QString::fromUtf8(argv[2]), std::stoi(argv[3]), argc, argv, std::stoi(argv[4]));
        if (argc == 3 && std::string(argv[1]) == "--benchmark") {
            const int count = std::stoi(argv[2]);
            Check(count > 0 && count <= 4096, "Benchmark count must be 1..4096");
            QTemporaryDir project;
            Check(project.isValid(), "Cannot create temporary benchmark project");
            QDir root(project.path());
            Check(root.mkpath("Assets"), "Cannot create benchmark Assets");
            std::string error;
            for (int i = 0; i < count; ++i)
                Check(AssetArtifactIO::Write(root.filePath("Assets/Item-" + QString::number(i) + ".txt"), QByteArray(256, 'A' + (i % 26)), error), error);
            const Json::Value cold = StartEditor(root, 1, count);
            Check(cold["Activity"]["ResourceLoads"] == count, "Cold benchmark skipped files");
            CheckWarm(cold, StartEditor(root, 2, count));
            std::cout << "PASS " << count << " temporary text resources: cold import and zero-work warm startup\n";
            return 0;
        }
        QTemporaryDir temporary;
        Check(temporary.isValid(), "Cannot create temporary project");
        QDir root(temporary.path());
        Check(root.mkpath("Assets/Nested"), "Cannot create fixture directory");
        QImage image(8, 8, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        Check(image.save(root.filePath("Assets/Main.png")) && image.save(root.filePath("Assets/Other.png")), "Cannot create textures");
        std::string error;
        Check(AssetArtifactIO::Write(root.filePath("Assets/Stable.txt"), "before\n", error)
            && AssetArtifactIO::Write(root.filePath("Assets/Nested/Sub.txt"), "nested\n", error), error);
        const Json::Value initial = StartEditor(root, 1);
        Check(initial["Activity"]["ResourceLoads"] == 4, "Initial scan did not import each file exactly once");
        Json::Value material;
        material["Version"] = 1;
        material["Texture"]["Guid"] = initial["Paths"]["Main.png"];
        material["Texture"]["LocalID"] = Manifest(initial, "Main.png")["Assets"][0]["LocalID"];
        WriteJson(root.filePath("Assets/Linked.mat"), material);
        const Json::Value baseline = StartEditor(root, 2);
        Check(baseline["Activity"]["ResourceLoads"] == 1, "Adding one material revalidated unrelated resources");
        const Json::Value warm = StartEditor(root, 3);
        CheckWarm(baseline, warm);

        const std::filesystem::path stable(root.filePath("Assets/Stable.txt").toStdU16String());
        std::filesystem::last_write_time(stable, std::filesystem::last_write_time(stable) + std::chrono::seconds(2));
        const Json::Value touched = StartEditor(root, 4);
        Check(touched["Activity"]["ResourceLoads"] == 1, "Timestamp-only change did not revalidate just the affected file");
        Check(Manifest(touched, "Stable.txt")["SourceHash"] == Manifest(warm, "Stable.txt")["SourceHash"], "Timestamp-only change altered content hash");
        Check(Manifest(touched, "Stable.txt")["Generation"] == Manifest(warm, "Stable.txt")["Generation"], "Timestamp-only change rebuilt identical contents");

        OverwriteSameStamp(root.filePath("Assets/Stable.txt"), "after!\n");
        const Json::Value changed = StartEditor(root, 5);
        Check(changed["Activity"]["ResourceLoads"] == 1, "Same-size/same-mtime content change was skipped or affected unrelated resources");
        Check(Manifest(changed, "Stable.txt")["SourceHash"] != Manifest(touched, "Stable.txt")["SourceHash"], "Change-time check missed altered content");
        Check(Manifest(changed, "Stable.txt")["Generation"] != Manifest(touched, "Stable.txt")["Generation"], "Changed contents did not rebuild the resource");

        image.fill(Qt::blue);
        Check(image.save(root.filePath("Assets/Main.png")), "Cannot change shared texture");
        const Json::Value dependency = StartEditor(root, 6);
        Check(dependency["Activity"]["ResourceLoads"].asInt() >= 2, "Texture edit did not invalidate its material dependency");
        Check(Manifest(dependency, "Linked.mat")["Generation"] != Manifest(changed, "Linked.mat")["Generation"], "Changed dependency reused stale material");

        Json::Value meta = ReadJson(root.filePath("Assets/Main.png.meta"));
        meta["ImportSettings"]["MaxSize"] = 2;
        WriteJson(root.filePath("Assets/Main.png.meta"), meta);
        const Json::Value settings = StartEditor(root, 7);
        Check(Manifest(settings, "Main.png")["SettingsHash"] != Manifest(dependency, "Main.png")["SettingsHash"], "Changed Meta settings were skipped");
        Check(Manifest(settings, "Linked.mat")["Generation"] != Manifest(dependency, "Linked.mat")["Generation"], "Changed dependency settings did not rebuild material");

        const auto artifactPath = [&](const Json::Value & state) {
            return root.filePath("Library/" + QString::fromStdString(Manifest(state, "Other.png")["Directory"].asString()) + "/"
                + QString::fromStdString(Manifest(state, "Other.png")["Assets"][0]["Path"].asString()));
        };
        Check(QFile::remove(artifactPath(settings)), "Cannot remove artifact");
        const Json::Value missing = StartEditor(root, 8);
        Check(missing["Activity"]["ResourceLoads"] == 1 && QFileInfo::exists(artifactPath(missing)), "Missing artifact did not trigger isolated repair");
        QFile artifact(artifactPath(missing));
        Check(artifact.open(QIODevice::ReadOnly), "Cannot read artifact fixture");
        QByteArray bytes = artifact.readAll();
        artifact.close();
        Check(!bytes.isEmpty(), "Artifact fixture is empty");
        bytes[0] = char(static_cast<unsigned char>(bytes[0]) ^ 0xff);
        OverwriteSameStamp(artifactPath(missing), bytes);
        const Json::Value damaged = StartEditor(root, 9);
        Check(damaged["Activity"]["ResourceLoads"] == 1, "Same-size/same-mtime artifact damage was skipped");
        QFile repaired(artifactPath(damaged));
        Check(repaired.open(QIODevice::ReadOnly), "Repaired artifact missing");
        Check(AssetArtifactIO::Hash(repaired.readAll()) == Manifest(damaged, "Other.png")["Assets"][0]["Hash"].asString(), "Artifact was not repaired correctly");

        Check(root.remove("Assets/Main.png.meta"), "Cannot remove Meta fixture");
        const Json::Value restored = StartEditor(root, 10);
        Check(restored["Paths"]["Main.png"] == settings["Paths"]["Main.png"], "Meta recovery changed GUID");
        Check(ReadJson(root.filePath("Assets/Main.png.meta"))["ImportSettings"]["MaxSize"] == 2, "Meta recovery lost settings");
        const Json::Value versioned = StartEditor(root, 11);
        Check(versioned["Activity"]["ResourceLoads"] == 1, "Importer version change did not invalidate only its asset");
        Check(Manifest(versioned, "Other.png")["Generation"] != Manifest(restored, "Other.png")["Generation"], "Importer version change reused stale artifact");
        const Json::Value finalWarm = StartEditor(root, 12);
        CheckWarm(versioned, finalWarm);

        const std::filesystem::path mainMeta(root.filePath("Assets/Main.png.meta").toStdU16String());
        std::filesystem::last_write_time(mainMeta, std::filesystem::last_write_time(mainMeta) + std::chrono::seconds(2));
        const Json::Value metaTouched = StartEditor(root, 13);
        Check(metaTouched["Activity"]["ResourceLoads"] == 2, "Meta timestamp-only change did not revalidate the texture and its material");
        Check(Manifest(metaTouched, "Main.png")["Generation"] == Manifest(finalWarm, "Main.png")["Generation"]
            && Manifest(metaTouched, "Linked.mat")["Generation"] == Manifest(finalWarm, "Linked.mat")["Generation"],
            "Meta timestamp-only change rebuilt unchanged texture/material contents");

        const Json::Value forced = StartEditor(root, 14);
        Check(forced["Activity"]["ResourceLoads"] == 1, "Explicit reimport affected unrelated resources");
        Check(Manifest(forced, "Stable.txt")["Generation"] != Manifest(metaTouched, "Stable.txt")["Generation"]
            && Manifest(forced, "Stable.txt")["SourceHash"] == Manifest(metaTouched, "Stable.txt")["SourceHash"]
            && Manifest(forced, "Stable.txt")["Assets"][0]["LocalID"] == Manifest(metaTouched, "Stable.txt")["Assets"][0]["LocalID"],
            "Explicit reimport did not rebuild identical contents while retaining resource identity");
        CheckWarm(forced, StartEditor(root, 15));
        const auto repairedManifest = StartEditor(root, 16);
        Check(repairedManifest["Activity"]["ResourceLoads"] == 1
            && Manifest(repairedManifest,"Other.png")["Generation"].isString()
            && Manifest(repairedManifest,"Other.png")["Generation"] != Manifest(forced,"Other.png")["Generation"]
            && Manifest(repairedManifest,"Other.png")["Assets"][0]["LocalID"] == Manifest(forced,"Other.png")["Assets"][0]["LocalID"],
            "Invalid cached manifest was accepted or not rebuilt from the current source Meta");
        CheckWarm(repairedManifest, StartEditor(root, 17));
        std::cout << "PASS independent warm starts: zero unchanged imports/writes, timestamp-only reuse, explicit force and source/dependency/Meta/artifact/version invalidation\n";
        return 0;
    } catch (const std::exception & error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}

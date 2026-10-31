#include "AssetTestSupport.h"
#include <QUuid>

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
    Json::Value snapshot;
    ILDBTable * table = library->GetLDBSession()->GetTable("AssetSources");
    Check(table != nullptr, "Source inventory table missing");
    Check(table->ForEach([&](const std::string & guid, const void * bytes, int length) {
        Json::Value value;
        Check(AssetArtifactIO::Parse(std::string(static_cast<const char *>(bytes), size_t(length)), value), "Invalid source row");
        snapshot["Sources"][guid] = value;
        snapshot["Paths"][value["Path"].asString()] = guid;
        return true;
    }), "Cannot enumerate source records");
    ILDBTable * backups = library->GetLDBSession()->GetTable("AssetSourceBackups");
    if (backups) Check(backups->ForEach([&](const std::string & key, const void * bytes, int length) {
        Json::Value value;
        Check(AssetArtifactIO::Parse(std::string(static_cast<const char *>(bytes), size_t(length)), value), "Invalid backup row");
        snapshot["Backups"][key] = value; return true;
    }), "Cannot enumerate backups");
    for (const std::string & guid : library->GetManifestGuids()) {
        AssetImportManifest manifest;
        Check(library->GetManifest(Guid(guid), manifest), "Invalid successful import manifest");
        snapshot["Manifests"][guid] = AssetArtifactIO::ToJson(manifest);
    }
    return snapshot;
}

void SimulateInterruptedPublications(ILibraryManager * library, const Json::Value & state) {
    const QDir root(QString::fromStdString(library->GetLibraryRoot()));
    for (const std::string & path : {"Lost.png", "Retry.png"}) {
        const bool committed = path == "Retry.png";
        const std::string guid = state["Paths"][path].asString();
        const Json::Value manifest = state["Manifests"][guid];
        const std::string generation = committed ? manifest["Generation"].asString()
            : QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        const QString folder = root.filePath(".Transactions/" + QString::fromStdString(generation));
        Check(QDir().mkpath(folder), "Cannot create interrupted publication fixture");
        const Json::Value asset = manifest["Assets"][0];
        const QString name = QString::fromStdString(asset["Path"].asString());
        Json::Value journal;
        journal["Version"] = 2; journal["Directory"] = "Artifact"; journal["Guid"] = guid; journal["Generation"] = generation;
        Json::Value file;
        file["Name"] = asset["Path"]; file["LocalID"] = asset["LocalID"]; file["HadOld"] = true;
        journal["Files"].append(file);
        WriteJson(QDir(folder).filePath("publication.json"), journal);
        std::string error;
        if (committed) {
            Check(AssetArtifactIO::Write(QDir(folder).filePath(name + ".previous"),"obsolete previous bytes",error), error);
            Check(library->GetLDBSession()->WriteBatch({{"Publications",generation,{1},false}}), "Cannot create publication commit marker");
        } else {
            Check(QFile::rename(root.filePath("Artifact/"+name),QDir(folder).filePath(name + ".previous")), "Cannot preserve previous artifact fixture");
            Check(AssetArtifactIO::Write(root.filePath("Artifact/"+name),"uncommitted replacement bytes",error), error);
        }
    }
}

int RunEditor(const QString & projectRoot, int phase, int argc, char ** argv) {
    Foundation::FoundationApi * real = Foundation::GetFoundationApi(TEST_BINARY_ROOT);
    Check(real && real->Initialize(argc, argv) && real->Launch(), "Foundation startup failed");
    TestIDE ide;
    TestProject project;
    project.Root = projectRoot.toStdString();
    TestConsole console;
    TestEditor editor;
    editor.IDE = &ide;
    editor.Modules["Project"] = &project;
    editor.Modules["Console"] = &console;
    FoundationProxy foundation;
    foundation.Real = real;
    foundation.Tasks.Real = real->TaskApi();
    foundation.Tasks.IDE = &ide;
    GameSproutEditor::GameSproutModuleApi api(&foundation, &editor);
    for (const std::string & name : {"LocalDatabase", "LibraryManager", "ShaderCompiler", "AssetsPipeline", "AssetsManager", "GuidCreator"})
        editor.Modules[name] = LoadModule(name);
    for (const auto & entry : editor.Modules) GameSproutEditor::GameSproutEditorFinal::Before(entry.second, &api);
    for (const auto & entry : editor.Modules) Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second, &api), "Module Initialize: " + entry.first);
    for (const auto & entry : editor.Modules) if (entry.first != "Project")
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second, &api), "Module Launch: " + entry.first);
    Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project, &api), "Project launch failed");
    auto * manager = static_cast<IAssetsManager *>(editor.Modules["AssetsManager"]);
    auto * library = static_cast<ILibraryManager *>(editor.Modules["LibraryManager"]);
    auto * pipeline = static_cast<IAssetsPipeline *>(editor.Modules["AssetsPipeline"]);
    auto wait = [&](size_t expected, bool startup = false) {
        const auto started = std::chrono::steady_clock::now();
        for (;;) {
            real->Update();
            QCoreApplication::processEvents();
            GameSproutEditor::GameSproutEditorFinal::Update(manager, &api);
            const Json::Value state = Snapshot(library);
            bool settled = state["Sources"].size() >= expected && ide.Count == 0;
            for (const Json::Value & source : state["Sources"])
                if (source["Status"] == "Pending") settled = false;
            if (settled) return state;
            if (startup) Check(ide.Count > 0, "Startup temporarily unlocked before scan/import completion");
            Check(std::chrono::steady_clock::now() - started < std::chrono::seconds(45), "Startup comparison did not settle");
            QThread::msleep(2);
        }
    };
    if (phase == 4) foundation.Tasks.Reject = true;
    if (phase == 1) {
        QImage late(4,4,QImage::Format_RGBA8888);
        late.fill(Qt::green);
        Check(late.save(QDir(projectRoot).filePath("Assets/FolderA/Late.png")), "Cannot create startup enumeration fixture");
    }
    if (phase == 1) foundation.Tasks.BeforeSubmission = [&] {
        Check(manager->GetAssetMetas().size() == 14, "Task submitted before the full GUID registry was available");
    };
    GameSproutEditor::GameSproutEditorFinal::Finished(manager, &api);
    const int startupLoadingTotal = ide.Loads;
    if (phase == 1) {
        Check(foundation.Tasks.Submissions == 13, "ScanDirectory returned before all 13 imports were submitted");
        Check(ide.Loads == 14, "Initial Loading denominator was not registered during the scan");
    }
    foundation.Tasks.BeforeSubmission = {};
    Json::Value state = wait(phase == 1 ? 14 : phase == 5 ? 12 : 13, true);
    if (phase == 1) Check(ide.Loads == startupLoadingTotal, "Startup denominator kept growing after ScanDirectory returned");
    const QDir root(projectRoot);
    if (phase == 1) {
        const Guid stableGuid(state["Paths"]["Stable.png"].asString());
        auto * stableSettings = static_cast<ITextureImportSettings *>(manager->QueryAsset(stableGuid)->GetImportSettings());
        Check(stableSettings->SetMaxSize(4) && manager->ReimportAsset(stableGuid), "Cannot configure backup settings fixture");
        const std::string texture = state["Paths"]["Move.png"].asString();
        Json::Value material;
        material["Version"] = 1;
        material["Texture"]["Guid"] = texture;
        material["Texture"]["LocalID"] = state["Manifests"][texture]["Assets"][0]["LocalID"];
        WriteJson(root.filePath("Assets/Linked.mat"), material);
        state = wait(15);
    }
    WriteJson(root.filePath("phase-" + QString::number(phase) + ".json"), state);
    if (phase == 2) SimulateInterruptedPublications(library, state);
    GameSproutEditor::GameSproutEditorFinal::Release(manager, &api);
    GameSproutEditor::GameSproutEditorFinal::Release(pipeline, &api);
    GameSproutEditor::GameSproutEditorFinal::Release(library, &api);
    Check(ide.Count == 0 && ide.Loads == ide.Unloads, "Restart Loading/Unloading imbalance");
    real->Release();
    return 0;
}

void StartEditor(const QDir & root, int phase) {
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({"--child", root.absolutePath(), QString::number(phase)});
    process.start();
    Check(process.waitForStarted(5000), "Cannot launch editor test process");
    // The child reports to its own pipe; each run has an independent Foundation,
    // module statics, file watcher and LMDB environment, exactly as after an exit.
    Check(process.waitForFinished(55000), "Editor test process timed out");
    const QByteArray output = process.readAllStandardOutput() + process.readAllStandardError();
    Check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
        "Editor phase failed: " + output.toStdString());
}

int main(int argc, char ** argv) {
    QGuiApplication application(argc, argv);
    try {
        if (argc == 4 && std::string(argv[1]) == "--child") return RunEditor(QString::fromUtf8(argv[2]), std::stoi(argv[3]), argc, argv);
        QTemporaryDir temporary;
        Check(temporary.isValid(), "Cannot create temporary project");
        QDir root(temporary.path());
        Check(root.mkpath("Assets/FolderA"), "Cannot create fixture folders");
        QImage image(8, 8, QImage::Format_RGBA8888);
        image.fill(QColor(20, 100, 200, 255));
        for (const QString & name : {"Stable", "Settings", "Move", "Deleted", "Broken", "InvalidMeta", "Lost", "MissingMeta", "FolderA/Nested"})
            Check(image.save(root.filePath("Assets/" + name + ".png")), "Cannot create PNG fixture");
        std::string error;
        for (const QString & name : {"Failed", "Retry"})
            Check(AssetArtifactIO::Write(root.filePath("Assets/" + name + ".png"), "invalid PNG", error), error);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Modify.txt"), "before\n", error), error);
        StartEditor(root, 1);
        const Json::Value first = ReadJson(root.filePath("phase-1.json"));
        const auto guid = [&](const std::string & path) { return first["Paths"][path].asString(); };
        Check(!guid("FolderA/Late.png").empty(), "Startup missed an enumerated resource");
        Check(first["Sources"][guid("Failed.png")]["Status"] == "Failed"
            && !first["Manifests"].isMember(guid("Failed.png")), "A failed first import was not recorded independently");
        Check(!first["Sources"][guid("Failed.png")]["SourceHash"].asString().empty(), "Failed source fingerprint missing");

        // Mutate Assets only while no editor process or watcher is running.
        Check(root.remove("Assets/Deleted.png") && root.remove("Assets/Deleted.png.meta"), "Cannot delete offline fixture");
        Check(QDir(root.filePath("Assets/FolderA")).removeRecursively(), "Cannot delete offline subtree");
        Check(root.remove("Assets/FolderA.meta"), "Cannot delete folder sidecar");
        Check(root.mkpath("Assets/FolderB"), "Cannot create offline move destination");
        Check(root.rename("Assets/Move.png", "Assets/FolderB/Renamed.png")
            && root.rename("Assets/Move.png.meta", "Assets/FolderB/Renamed.png.meta"), "Cannot move offline fixture");
        const std::filesystem::path modified = std::filesystem::u8path(root.filePath("Assets/Modify.txt").toStdString());
        const auto originalTime = std::filesystem::last_write_time(modified);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Modify.txt"), "after!\n", error), error);
        std::filesystem::last_write_time(modified, originalTime); // Same length and timestamp, different contents.
        Json::Value settings = ReadJson(root.filePath("Assets/Settings.png.meta"));
        settings["ImportSettings"]["MaxSize"] = 2;
        WriteJson(root.filePath("Assets/Settings.png.meta"), settings);
        Check(AssetArtifactIO::Write(root.filePath("Assets/Broken.png"), "invalid PNG", error), error);
        Check(AssetArtifactIO::Write(root.filePath("Assets/InvalidMeta.png.meta"), "{invalid", error), error);
        const Json::Value lost = first["Manifests"][guid("Lost.png")];
        Check(root.remove("Library/" + QString::fromStdString(lost["Directory"].asString()) + "/"
            + QString::fromStdString(lost["Assets"][0]["Path"].asString())), "Cannot remove cached artifact");
        Check(image.save(root.filePath("Assets/Retry.png")) && image.save(root.filePath("Assets/New.png")), "Cannot create offline PNG");
        Check(root.remove("Assets/MissingMeta.png.meta"), "Cannot remove sidecar");
        Check(QFile::copy(root.filePath("Assets/Stable.png"), root.filePath("Assets/Conflict.png"))
            && QFile::copy(root.filePath("Assets/Stable.png.meta"), root.filePath("Assets/Conflict.png.meta")), "Cannot create duplicate GUID fixture");
        StartEditor(root, 2);
        const Json::Value second = ReadJson(root.filePath("phase-2.json"));
        const auto manifest = [&](const Json::Value & state, const std::string & path) { return state["Manifests"][guid(path)]; };
        Check(second["Paths"]["New.png"].isString(), "Offline addition was not discovered");
        for (const std::string & path : {"Deleted.png", "FolderA", "FolderA/Nested.png", "FolderA/Late.png"})
            Check(!second["Sources"].isMember(guid(path)) && !second["Manifests"].isMember(guid(path)), "Offline deletion retained a source/manifest");
        Check(second["Paths"]["FolderB/Renamed.png"] == guid("Move.png")
            && manifest(second,"Move.png")["Assets"][0]["LocalID"] == manifest(first,"Move.png")["Assets"][0]["LocalID"], "Offline move changed GUID or LocalID");
        Check(ReadJson(root.filePath("Assets/FolderB/Renamed.png.meta"))["Path"] == "FolderB/Renamed.png", "Moved Meta has stale relative path");
        Check(manifest(second,"Stable.png")["Generation"] == manifest(first,"Stable.png")["Generation"], "GUID conflict replaced good output");
        Check(second["Sources"][guid("Stable.png")]["Status"] == "Failed"
            && second["Sources"][guid("Stable.png")]["Path"] == "Stable.png", "GUID conflict replaced the source inventory");
        Check(manifest(second,"Modify.txt")["SourceHash"] != manifest(first,"Modify.txt")["SourceHash"], "Same timestamp/length edit was missed");
        Check(manifest(second,"Settings.png")["SettingsHash"] != manifest(first,"Settings.png")["SettingsHash"], "Offline setting change was missed");
        Check(manifest(second,"Lost.png")["Generation"] != manifest(first,"Lost.png")["Generation"], "Missing Library artifact was not repaired");
        Check(second["Paths"]["MissingMeta.png"] == guid("MissingMeta.png"), "Missing sidecar recovery changed GUID");
        for (const std::string & path : {"Failed.png", "Broken.png", "InvalidMeta.png"})
            Check(second["Sources"][guid(path)]["Status"] == "Failed", "Failed source was considered up to date");
        for (const std::string & path : {"Broken.png", "InvalidMeta.png"})
            Check(manifest(second,path)["Generation"] == manifest(first,path)["Generation"], "Failure replaced the good generation");
        Check(second["Sources"][guid("Retry.png")]["Status"] == "Ready", "Previous failed import was not retried");
        const Json::Value dependencies = manifest(second,"Linked.mat")["Dependencies"];
        Check(dependencies.size() == 1 && dependencies[0]["Path"] == "FolderB/Renamed.png", "Moved dependency index was not updated");

        Check(root.remove("Assets/Conflict.png") && root.remove("Assets/Conflict.png.meta"), "Cannot resolve duplicate GUID fixture");
        Check(image.save(root.filePath("Assets/Failed.png")) && image.save(root.filePath("Assets/Broken.png")), "Cannot repair PNG fixtures");
        Json::Value repairedMeta;
        Check(AssetArtifactIO::Parse(first["Sources"][guid("InvalidMeta.png")]["MetaJson"].asString(), repairedMeta), "Missing last valid Meta snapshot");
        WriteJson(root.filePath("Assets/InvalidMeta.png.meta"), repairedMeta);
        const auto modifyTime = std::filesystem::last_write_time(modified);
        std::filesystem::last_write_time(modified, modifyTime + std::chrono::seconds(2));
        StartEditor(root, 3);
        const Json::Value third = ReadJson(root.filePath("phase-3.json"));
        for (const Json::Value & record : third["Sources"])
            Check(record["Status"] == "Ready" || record["Status"] == "NotRequired", "Repaired project did not recover after reopening");
        Check(manifest(third,"Modify.txt")["SourceHash"] == manifest(second,"Modify.txt")["SourceHash"]
            && manifest(third,"Modify.txt")["Generation"] != manifest(second,"Modify.txt")["Generation"], "Timestamp-only modification did not trigger preprocessing");
        Check(manifest(third,"Retry.png")["Generation"] == manifest(second,"Retry.png")["Generation"], "Recovery rolled back a committed flat artifact");
        Check(manifest(third,"Lost.png")["Generation"] == manifest(second,"Lost.png")["Generation"], "Repeated startup rebuilt an unchanged asset");
        Check(manifest(third,"InvalidMeta.png")["Generation"] != manifest(first,"InvalidMeta.png")["Generation"], "Changed Meta timestamp did not trigger preprocessing");
        Check(root.remove("Assets/Stable.png"), "Cannot delete scan failure fixture");
        const auto protectedFolder = std::filesystem::u8path(root.filePath("Assets/FolderB").toStdString());
        const auto originalPermissions = std::filesystem::status(protectedFolder).permissions();
        std::filesystem::permissions(protectedFolder, std::filesystem::perms::none);
        StartEditor(root, 4); // Incomplete directory enumeration must not confirm deletions.
        std::filesystem::permissions(protectedFolder, originalPermissions);
        const Json::Value incomplete = ReadJson(root.filePath("phase-4.json"));
        Check(incomplete["Sources"].isMember(guid("Stable.png")) && incomplete["Manifests"].isMember(guid("Stable.png")), "Incomplete scan removed a good record");
        StartEditor(root, 5);
        const Json::Value completed = ReadJson(root.filePath("phase-5.json"));
        Check(!completed["Sources"].isMember(guid("Stable.png")) && !completed["Manifests"].isMember(guid("Stable.png")), "Successful scan did not finish deferred deletion");
        Check(!QFileInfo::exists(root.filePath("Assets/Stable.png.meta")), "Orphan Meta was not removed after backup");
        bool archived = false;
        for (const Json::Value & value : completed["Backups"]) if (value["Record"]["Guid"] == guid("Stable.png")) {
            archived = true;
            Check(value["Record"]["MetaJson"].isString() && value["Manifest"].isObject(), "Backup omitted Meta or last successful manifest");
        }
        Check(archived, "Deleted source was not backed up");
        Check(image.save(root.filePath("Assets/Stable.png")), "Cannot restore source without Meta");
        StartEditor(root, 6);
        const Json::Value restored = ReadJson(root.filePath("phase-6.json"));
        Check(restored["Paths"]["Stable.png"] == guid("Stable.png"), "Backup recovery changed GUID");
        Check(ReadJson(root.filePath("Assets/Stable.png.meta"))["ImportSettings"]["MaxSize"] == 4, "Backup recovery lost import settings");
        Check(manifest(restored,"Stable.png")["Assets"][0]["LocalID"] == manifest(first,"Stable.png")["Assets"][0]["LocalID"], "Backup recovery changed LocalID");
        Check(QDir(root.filePath("Library/.Transactions")).entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty(), "Recovered publication directories were retained");
        std::cout << "PASS six independent editor launches: database-first reconciliation, timestamp-only changes, atomic backups, orphan cleanup and Meta recovery\n";
        return 0;
    } catch (const std::exception & error) {
        std::cerr << "FAIL " << error.what() << "\n";
        return 1;
    }
}

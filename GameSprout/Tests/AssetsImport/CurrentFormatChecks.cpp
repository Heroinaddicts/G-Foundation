#include "AssetTestSupport.h"
#include <QElapsedTimer>
#include <algorithm>
#include <map>

namespace {
QByteArray ReadBytes(const QString& path) {
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly), "Cannot read current-format fixture: " + path.toStdString());
    return file.readAll();
}

std::size_t RowCount(ILDBTable* table) {
    Check(table != nullptr, "Missing source inventory table");
    std::size_t count = 0;
    Check(table->ForEach([&](const std::string&, const void*, int) { ++count; return true; }),
        "Cannot enumerate source inventory");
    return count;
}
}

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    Foundation::FoundationApi* real = nullptr;
    TestIDE ide;
    TestProject project;
    TestConsole console;
    TestEditor editor;
    FoundationProxy foundation;
    GameSproutEditor::GameSproutModuleApi* api = nullptr;
    std::vector<GameSproutEditor::IGameSproutModule*> initialized;
    const auto cleanup = [&] {
        for (const std::string name : {"AssetsManager", "AssetsPipeline", "ShaderCompiler", "LibraryManager", "LocalDatabase", "GuidCreator"}) {
            const auto found = editor.Modules.find(name);
            if (found != editor.Modules.end()
                && std::find(initialized.begin(), initialized.end(), found->second) != initialized.end())
                GameSproutEditor::GameSproutEditorFinal::Release(found->second, api);
        }
        initialized.clear();
        delete api; api = nullptr;
        if (real) { real->Release(); real = nullptr; }
    };
    try {
        Check(temporary.isValid(), "Cannot create current-format project");
        QDir root(temporary.path());
        Check(root.mkpath("Assets"), "Cannot create current-format Assets directory");
        real = Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(real && real->Initialize(argc, argv) && real->Launch(), "Foundation startup failed");
        project.Root = temporary.path().toStdString();
        editor.IDE = &ide;
        editor.Modules["Project"] = &project;
        editor.Modules["Console"] = &console;
        foundation.Real = real;
        foundation.Tasks.Real = real->TaskApi();
        foundation.Tasks.IDE = &ide;
        api = new GameSproutEditor::GameSproutModuleApi(&foundation, &editor);
        for (const std::string name : {"LocalDatabase", "LibraryManager", "ShaderCompiler", "AssetsPipeline", "AssetsManager", "GuidCreator"})
            editor.Modules[name] = LoadModule(name);
        for (const auto& entry : editor.Modules)
            GameSproutEditor::GameSproutEditorFinal::Before(entry.second, api);
        for (const auto& entry : editor.Modules) {
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second, api), "Initialize " + entry.first);
            initialized.push_back(entry.second);
        }
        // Delay AssetsManager's launch: publish a Library generation before the
        // source inventory exists, then observe its actual startup behavior.
        for (const auto& entry : editor.Modules) if (entry.first != "Project" && entry.first != "AssetsManager")
            Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second, api), "Launch " + entry.first);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&project, api), "Project launch failed");
        auto* assets = static_cast<IAssetsManager*>(editor.Modules["AssetsManager"]);
        auto* library = static_cast<ILibraryManager*>(editor.Modules["LibraryManager"]);
        auto* pipeline = static_cast<IAssetsPipeline*>(editor.Modules["AssetsPipeline"]);
        std::string stage = "isolated Library generation";
        const auto pump = [&](const std::function<bool()>& finished) {
            QElapsedTimer timer; timer.start();
            do {
                real->Update();
                QCoreApplication::processEvents();
                GameSproutEditor::GameSproutEditorFinal::Update(assets, api);
                if (finished()) return;
                QThread::msleep(2);
            } while (timer.elapsed() < 30000);
            throw std::runtime_error("Current-format timeout: " + stage);
        };
        QImage image(8, 4, QImage::Format_RGBA8888);
        image.fill(QColor(32, 120, 210));
        Check(image.save(root.filePath("Assets/Unrecorded.png")), "Cannot create isolated texture");
        TestMeta unrecorded(eAssetImporterType::Texture, Guid(11, 12, 13, 14), "Unrecorded.png");
        std::string json, error;
        Check(unrecorded.SerializeMeta(json)
            && AssetArtifactIO::Write(root.filePath("Assets/Unrecorded.png.meta"), QByteArray::fromStdString(json), error), error);
        bool completed = false;
        AssetImportResult imported;
        pipeline->ImportAsset(&unrecorded, [&](const AssetImportResult& result) { imported = result; completed = true; });
        pump([&] { return completed; });
        Check(imported.Success && !imported.Manifest.Generation.empty(), "Direct texture import failed: " + imported.Error);
        Check(ide.Count == 0, "Direct import left Loading active");
        auto* database = library->GetLDBSession();
        Check(database && database->CreateTable("AssetSources") && database->CreateTable("AssetSourceBackups"),
            "Cannot create source inventory tables");
        Check(RowCount(database->GetTable("AssetSources")) == 0 && RowCount(database->GetTable("AssetSourceBackups")) == 0,
            "Direct Pipeline import unexpectedly populated the source inventory");
        Check(QFile::remove(root.filePath("Assets/Unrecorded.png"))
            && QFile::remove(root.filePath("Assets/Unrecorded.png.meta")), "Cannot remove isolated source");

        stage = "source inventory startup";
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(assets, api), "AssetsManager launch failed");
        GameSproutEditor::GameSproutEditorFinal::Finished(assets, api);
        pump([&] { return ide.Count == 0; });
        Check(assets->GetAssetMetas().empty(), "Empty Assets produced a runtime Meta");
        Check(RowCount(database->GetTable("AssetSources")) == 0 && RowCount(database->GetTable("AssetSourceBackups")) == 0,
            "Startup migrated an unrecorded Library generation into the source inventory/backups");
        AssetImportManifest retained;
        Check(library->GetManifest(unrecorded._Guid, retained) && retained.Generation == imported.Manifest.Generation,
            "Startup treated an unrecorded Library generation as a deleted source");

        stage = "current and obsolete Meta inputs";
        const Guid currentGuid(21, 22, 23, 24);
        TestMeta current(eAssetImporterType::Texture, currentGuid, "Current.png");
        Check(current.SerializeMeta(json)
            && AssetArtifactIO::Write(root.filePath("Assets/Current.png.meta"), QByteArray::fromStdString(json), error)
            && image.save(root.filePath("Assets/Current.png")), "Cannot create current Meta fixture: " + error);
        std::map<std::string, QByteArray> obsolete;
        const std::vector<std::string> names{"OldType.png", "OldVersion.png", "NoVersion.png"};
        for (std::size_t index = 0; index < names.size(); ++index) {
            TestMeta candidate(eAssetImporterType::Texture, Guid(31 + static_cast<uint32_t>(index), 32, 33, 34), names[index]);
            Check(candidate.SerializeMeta(json), "Cannot serialize rejection fixture");
            Json::Value value;
            Check(AssetArtifactIO::Parse(json, value), "Cannot parse rejection fixture");
            if (index == 0) { value["AssetType"] = value["ImporterType"]; value.removeMember("ImporterType"); }
            if (index == 1) value["Version"] = 0;
            if (index == 2) value.removeMember("Version");
            const QByteArray bytes = QByteArray::fromStdString(AssetArtifactIO::JsonText(value));
            obsolete.emplace(names[index], bytes);
            Check(AssetArtifactIO::Write(root.filePath("Assets/" + QString::fromStdString(names[index]) + ".meta"), bytes, error)
                && image.save(root.filePath("Assets/" + QString::fromStdString(names[index]))), "Cannot create obsolete Meta fixture: " + error);
        }
        pump([&] {
            if (ide.Count != 0) return false;
            IAssetMeta* meta = assets->QueryAsset(currentGuid);
            if (!meta || meta->GetImportState() != eAssetImportState::Ready) return false;
            for (const auto& fixture : obsolete) {
                bool rejected = false;
                for (const std::string& message : console.Errors)
                    if (message.find("Cannot load source Meta:") != std::string::npos
                        && message.find(fixture.first) != std::string::npos) { rejected = true; break; }
                if (!rejected) return false;
            }
            return true;
        });
        Check(assets->GetAssetMetas().size() == 1 && RowCount(database->GetTable("AssetSources")) == 1,
            "Unsupported Meta was accepted or replaced with a newly assigned GUID");
        auto* settings = static_cast<ITextureImportSettings*>(assets->QueryAsset(currentGuid)->GetImportSettings());
        Check(settings && settings->GetMaxSize() == 2048 && settings->GetGenerateMipmaps()
            && settings->GetColorSpace() == eTextureColorSpace::SRGB, "Current empty ImportSettings lost normal defaults");
        for (const auto& fixture : obsolete)
            Check(ReadBytes(root.filePath("Assets/" + QString::fromStdString(fixture.first) + ".meta")) == fixture.second,
                "Unsupported Meta was rewritten: " + fixture.first);
        Check(ide.Count == 0 && ide.Loads == ide.Unloads, "Current-format checks left Loading active");
        cleanup();
        std::cout << "PASS current Meta/defaults, obsolete field/version rejection without rewrite, and no source-inventory migration\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL " << exception.what() << '\n';
        cleanup();
        return 1;
    }
}

#include "../AssetsImport/AssetTestSupport.h"
#include <chrono>

class ProjectFixture {
public:
    ProjectFixture() : Api(&Foundation, &Editor) {}
    ~ProjectFixture() { Shutdown(); }
    ProjectFixture(const ProjectFixture&) = delete;
    ProjectFixture& operator=(const ProjectFixture&) = delete;

    void Initialize(int argc, char** argv) {
        Real = Foundation::GetFoundationApi(TEST_BINARY_ROOT);
        Check(Real && Real->Initialize(argc, argv) && Real->Launch(), "Foundation startup failed");
        Foundation.Real = Real;
        Foundation.Tasks.Real = Real->TaskApi();
        Foundation.Tasks.IDE = &IDE;
        Check(Temporary.isValid(), "Temporary project failed");
        const QDir root(Temporary.path());
        Check(root.mkpath("Assets"), "Assets creation failed");
        std::string error;
        for (const QString& name : {"one.txt", "two.txt", "three.txt"})
            Check(AssetArtifactIO::Write(root.filePath("Assets/" + name), "resource " + name.toUtf8(), error), error);
        Project.Root = Temporary.path().toStdString();
        Editor.IDE = &IDE;
        Editor.Modules["Project"] = &Project;
        Editor.Modules["Console"] = &Console;
        for (const std::string& name : {"LocalDatabase", "LibraryManager", "ShaderCompiler", "AssetsPipeline", "AssetsManager", "GuidCreator"})
            Editor.Modules[name] = LoadModule(name);
        Manager = static_cast<IAssetsManager*>(Editor.Modules["AssetsManager"]);
        Pipeline = static_cast<IAssetsPipeline*>(Editor.Modules["AssetsPipeline"]);
        Library = static_cast<ILibraryManager*>(Editor.Modules["LibraryManager"]);
        for (const auto& entry : Editor.Modules) GameSproutEditor::GameSproutEditorFinal::Before(entry.second, &Api);
        for (const auto& entry : Editor.Modules)
            Check(GameSproutEditor::GameSproutEditorFinal::Initialize(entry.second, &Api), "Module Initialize failed: " + entry.first);
        for (const auto& entry : Editor.Modules) if (entry.first != "Project")
            Check(GameSproutEditor::GameSproutEditorFinal::Launch(entry.second, &Api), "Module Launch failed: " + entry.first);
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(&Project, &Api), "Project launch failed");
    }

    void Pump(const std::function<bool()>& done) {
        const auto start = std::chrono::steady_clock::now();
        do {
            Real->Update();
            QCoreApplication::processEvents();
            GameSproutEditor::GameSproutEditorFinal::Update(Manager, &Api);
            if (done()) return;
            QThread::msleep(1);
        } while (std::chrono::steady_clock::now() - start < std::chrono::seconds(15));
        throw std::runtime_error("Timed out waiting for pipeline progress, Loading=" + std::to_string(IDE.Count));
    }

    void CheckSettled() {
        Check(IDE.Count == 0 && IDE.Loads == IDE.Unloads, "Pipeline Loading leaked");
        Check(IDE.InvalidProgressOperations == 0, "Pipeline used stale or invalid progress token");
        Check(IDE.AbandonedImportPlans == 0, "Loading reached zero before its fixed plan was ended");
        for (const auto& entry : IDE.ImportPlans)
            Check(entry.second.Ended && entry.second.Completed == entry.second.Total, "Pipeline left an unfinished plan");
    }

    void Shutdown() {
        if (!Real) return;
        if (Manager) GameSproutEditor::GameSproutEditorFinal::Release(Manager, &Api);
        if (Pipeline && !PipelineReleased) GameSproutEditor::GameSproutEditorFinal::Release(Pipeline, &Api);
        if (Library) GameSproutEditor::GameSproutEditorFinal::Release(Library, &Api);
        Real->Release();
        Real = nullptr;
    }

    Foundation::FoundationApi* Real = nullptr;
    QTemporaryDir Temporary;
    TestIDE IDE;
    TestProject Project;
    TestConsole Console;
    TestEditor Editor;
    FoundationProxy Foundation;
    GameSproutEditor::GameSproutModuleApi Api;
    IAssetsManager* Manager = nullptr;
    IAssetsPipeline* Pipeline = nullptr;
    ILibraryManager* Library = nullptr;
    bool PipelineReleased = false;
};

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    try {
        ProjectFixture fixture;
        fixture.Initialize(argc, argv);
        auto& ide = fixture.IDE;
        auto* pipeline = fixture.Pipeline;
        Check(ide.ImportBegins.empty(), "Startup Loading barrier was counted as resource progress");

        GameSproutEditor::GameSproutEditorFinal::Finished(fixture.Manager, &fixture.Api);
        Check(ide.ImportBegins.size() == 1, "Startup should register one resource plan");
        const uint64_t startupToken = ide.ImportBegins.back();
        Check(ide.ImportPlans.at(startupToken).Total == 3 && ide.ImportPlans.at(startupToken).Completed == 0,
            "Startup denominator was not registered completely before worker completions");
        const int registeredLoads = ide.Loads;
        GameSproutEditor::GameSproutEditorFinal::Finished(fixture.Library, &fixture.Api);
        Check(ide.Loads == registeredLoads + 1 && ide.ImportBegins.size() == 1,
            "Artifact GC changed the resource plan");
        fixture.Pump([&] { return ide.Count == 0; });
        fixture.CheckSettled();
        Check(fixture.Console.Errors.empty(), "Initial resources failed to import");
        std::vector<IAssetMeta*> metas;
        for (const std::string& name : {"one.txt", "two.txt", "three.txt"}) {
            IAssetMeta* found = nullptr;
            for (IAssetMeta* meta : fixture.Manager->GetAssetMetas()) if (meta->GetPath() == name) found = meta;
            Check(found != nullptr, "Scanned resource missing");
            metas.push_back(found);
        }
        std::cout << "PASS one fixed three-resource startup plan, no barrier/GC contributions\n";

        // With no startup barrier, the registration guard must survive synchronous rejection.
        fixture.Foundation.Tasks.Reject = true;
        const std::size_t beforeRejected = ide.ImportBegins.size();
        const uint64_t rejected = pipeline->BeginImportBatch(3);
        int rejectedCallbacks = 0;
        for (IAssetMeta* meta : metas) pipeline->ImportAsset(meta, [&](const AssetImportResult& result) {
            Check(!result.Success, "Synchronous rejection succeeded"); ++rejectedCallbacks;
        });
        Check(ide.Count == 1 && rejectedCallbacks == 3, "Registration guard was not held across rejection callbacks");
        Check(ide.ImportBegins.size() == beforeRejected + 1 && ide.ImportPlans.at(ide.ImportBegins.back()).Total == 3,
            "Synchronous rejection grew the planned denominator");
        pipeline->EndImportBatch(rejected);
        fixture.Foundation.Tasks.Reject = false;
        fixture.CheckSettled();
        std::cout << "PASS all synchronous rejections complete the original fixed plan\n";

        int cancelledCallbacks = 0;
        const uint64_t cancelled = pipeline->BeginImportBatch(3);
        for (IAssetMeta* meta : metas) {
            pipeline->ImportAsset(meta, [&](const AssetImportResult& result) {
                Check(!result.Success && result.Superseded, "Cancelled task published a result"); ++cancelledCallbacks;
            });
            pipeline->Invalidate(meta->_Guid);
        }
        pipeline->EndImportBatch(cancelled);
        fixture.Pump([&] { return cancelledCallbacks == 3 && ide.Count == 0; });
        fixture.CheckSettled();
        std::cout << "PASS asynchronous cancellation balances progress and Loading\n";

        // A decoder failure still completes its single-item plan.
        TestMeta invalidTexture(eAssetImporterType::Texture, Guid(15, 25, 35, 45), "one.txt");
        bool failed = false;
        pipeline->ImportAsset(&invalidTexture, [&](const AssetImportResult& result) {
            Check(!result.Success && !result.Superseded && !result.Error.empty(), "Expected decoder failure missing"); failed = true;
        });
        fixture.Pump([&] { return failed && ide.Count == 0; });
        fixture.CheckSettled();

        const std::size_t beforeRetry = ide.ImportBegins.size();
        const uint64_t retryBatch = pipeline->BeginImportBatch(3);
        int initialCallbacks = 0, retries = 0;
        bool nestedCreated = false;
        fixture.Foundation.Tasks.BeforeCompletion = [&] { pipeline->Invalidate(metas[0]->_Guid); };
        for (std::size_t i = 0; i < metas.size(); ++i) pipeline->ImportAsset(metas[i], [&, i](const AssetImportResult& result) {
            ++initialCallbacks;
            if (i == 0) {
                Check(!result.Success && result.Superseded, "Superseded initial result was not discarded");
                nestedCreated = true;
                pipeline->ImportAsset(metas[0], [&](const AssetImportResult& retry) {
                    Check(retry.Success, "Nested retry failed"); ++retries;
                });
            } else Check(result.Success, "Other initial resource failed");
        });
        pipeline->EndImportBatch(retryBatch);
        fixture.Pump([&] { return initialCallbacks == 3 && retries == 1 && ide.Count == 0; });
        Check(nestedCreated && ide.ImportBegins.size() == beforeRetry + 2,
            "Nested retry was not represented by a separate plan");
        Check(ide.ImportPlans.at(ide.ImportBegins[beforeRetry]).Total == 3
            && ide.ImportPlans.at(ide.ImportBegins[beforeRetry + 1]).Total == 1,
            "Nested retry changed the original fixed denominator");
        fixture.CheckSettled();
        std::cout << "PASS callback retry creates a separate one-item plan without growing its parent\n";

        const uint64_t partial = pipeline->BeginImportBatch(3);
        bool partialDone = false;
        pipeline->ImportAsset(metas[1], [&](const AssetImportResult& result) {
            Check(result.Success, "Partial registration task failed"); partialDone = true;
        });
        pipeline->EndImportBatch(partial);
        const auto& partialPlan = ide.ImportPlans.at(ide.ImportBegins.back());
        Check(partialPlan.Total == 3 && partialPlan.Completed == 2 && !partialPlan.Ended,
            "Unsubmitted cancelled slots did not retain the fixed plan");
        fixture.Pump([&] { return partialDone && ide.Count == 0; });
        fixture.CheckSettled();

        // Release with both registered tasks and an unsubmitted slot; callbacks are suppressed.
        const uint64_t abandonedRegistration = pipeline->BeginImportBatch(3);
        Check(abandonedRegistration != 0, "Release plan not registered");
        int lateCallbacks = 0;
        for (std::size_t i = 0; i < 2; ++i)
            pipeline->ImportAsset(metas[i], [&](const AssetImportResult&) { ++lateCallbacks; });
        GameSproutEditor::GameSproutEditorFinal::Release(pipeline, &fixture.Api);
        fixture.PipelineReleased = true;
        fixture.CheckSettled();
        for (int i = 0; i < 15; ++i) { fixture.Real->Update(); QThread::msleep(1); }
        Check(lateCallbacks == 0, "Released owner received callbacks");
        fixture.CheckSettled();
        std::cout << "PASS open-batch Release cancels every planned slot before invalidating UI tokens\n";
        fixture.Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}

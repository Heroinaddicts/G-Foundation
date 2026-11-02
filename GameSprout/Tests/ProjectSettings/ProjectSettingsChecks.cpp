#include "ProjectSettings.h"
#include "ProjectSettingsFormat.h"
#include "IProject.h"
#include "IPlayMode.h"
#include "ISceneManager.h"
#include "GameSproutRuntimeExport.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule& module, GameSproutModuleApi& api) { module.BeforeInitialize(&api); }
    static bool Initialize(IGameSproutModule& module, GameSproutModuleApi& api) { return module.Initialize(&api); }
    static void Release(IGameSproutModule& module, GameSproutModuleApi& api) { module.Release(&api); }
};
}
static void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
template<class T> class Module : public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override { return true; }
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override { return true; }
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};
class Project final : public Module<IProject> {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override { return Root; }
};
class PlayMode final : public Module<IPlayMode> {
public:
    PlayModeState State;
    PlayModeState GetState() const override { return State; }
    bool Play(std::string&) override { return false; }
    bool Pause(std::string&) override { return false; }
    bool Resume(std::string&) override { return false; }
    bool Step(std::string&) override { return false; }
    bool Stop(std::string&) override { return false; }
    bool RegisterPlayModeListener(IPlayModeListener*) override { return false; }
    void UnregisterPlayModeListener(IPlayModeListener*) override {}
};
class SceneManager final : public Module<ISceneManager> {
public:
    std::vector<std::uint64_t> ReferencedTags;
    std::string Path;
    GameSproutRuntime::IScene* GetActiveScene() const override { return nullptr; }
    const std::string& GetActiveScenePath() const override { return Path; }
    bool OpenScene(const std::string&, std::string&) override { return false; }
    std::vector<std::uint64_t> GetReferencedTagIDs() const override { return ReferencedTags; }
    bool EditObject(std::uint64_t, const std::string&, const fSceneEditOperation&, const fSceneEditOperation&, std::string&) override { return false; }
    bool Undo(std::string&) override { return false; }
    bool Redo(std::string&) override { return false; }
    bool CanUndo() const override { return false; }
    bool CanRedo() const override { return false; }
    bool SaveScene(std::string&) override { return false; }
    bool SaveSceneAs(const std::string&, std::string&) override { return false; }
    bool HasUnsavedChanges() const override { return false; }
    bool CreateSceneAsset(const std::string&, const std::string&, std::string&, std::string&) override { return false; }
    bool RegisterSceneListener(ISceneListener*) override { return false; }
    void UnregisterSceneListener(ISceneListener*) override {}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime) : GameSproutViewApi(runtime) {}
    std::map<std::string, GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        const auto found = Modules.find(name); return found == Modules.end() ? nullptr : found->second;
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&) const override { return nullptr; }
};
class Listener final : public IProjectSettingsListener {
public:
    IProjectSettings* Settings = nullptr;
    unsigned Calls = 0;
    QThread* Thread = nullptr;
    bool RejectReentry = false;
    void OnProjectSettingsChanged(const GameSproutRuntime::RuntimeProjectSettings& data, std::uint64_t revision) override {
        ++Calls; Thread = QThread::currentThread();
        if (Settings) { std::string error; RejectReentry = !Settings->SaveSettings(data, revision, error); }
    }
};
static QByteArray Read(const QString& path) {
    QFile file(path); Check(file.open(QIODevice::ReadOnly), "Cannot read fixture"); return file.readAll();
}
static void Write(const QString& path, const QByteArray& bytes) {
    QFile file(path); Check(file.open(QIODevice::WriteOnly), "Cannot write fixture"); Check(file.write(bytes) == bytes.size(), "Fixture write failed");
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    GameSproutRuntime::IGameSproutRuntime* runtime = nullptr;
    try {
        runtime = Export(RUNTIME_LIBRARY_DIRECTORY); Check(runtime, "Runtime unavailable");
        QTemporaryDir temporary; Check(temporary.isValid(), "Temporary project unavailable");
        QDir root(temporary.path()); Check(root.mkpath("Assets"), "Assets unavailable");
        Project project; project.Root = temporary.path().toStdString(); PlayMode play; SceneManager scenes; Editor editor(runtime);
        editor.Modules = {{"Project", &project}, {"PlayMode", &play}, {"SceneManager", &scenes}};
        GameSproutEditor::GameSproutModuleApi api(nullptr, &editor); ProjectSettings module;
        GameSproutEditor::GameSproutEditorFinal::Before(module, api);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(module, api), "Settings initialization failed");
        const auto path = root.filePath("ProjectSettings/ProjectSettings.json");
        const auto original = Read(path);
        Check(module.GetRevision() == 1 && module.GetSettings().LayerNames[1] == "Ignore Raycast", "Default settings differ");
        Listener listener; listener.Settings = &module; Check(module.RegisterSettingsListener(&listener), "Cannot subscribe");
        auto settings = module.GetSettings(); settings.LayerNames[3] = "Character";
        settings.Tags.push_back({1, "Player"}); settings.Tags.push_back({2, "Enemy"}); settings.NextTagID = 3;
        settings.CollisionMatrix[3] &= ~(1u << 2); settings.CollisionMatrix[2] &= ~(1u << 3);
        std::string error; Check(module.SaveSettings(settings, 1, error), error);
        Check(module.GetRevision() == 2 && listener.Calls == 1 && listener.RejectReentry && listener.Thread == QThread::currentThread(), "Settings event timing differs");
        Check(runtime->GetProjectSettings() == settings && Read(path) != original, "Runtime/disk did not publish");
        const auto saved = Read(path);
        Check(!module.SaveSettings(settings, 1, error) && Read(path) == saved, "Stale revision overwrote settings");
        auto invalid = settings; invalid.LayerNames[0] = "Other";
        Check(!module.SaveSettings(invalid, 2, error), "Built-in rename accepted");
        invalid = settings; invalid.CollisionMatrix[3] ^= (1u << 2);
        Check(!module.SaveSettings(invalid, 2, error), "Asymmetric collision matrix accepted");
        invalid = settings; invalid.Tags.push_back({3, "Player"}); invalid.NextTagID = 4;
        Check(!module.SaveSettings(invalid, 2, error), "Duplicate Tag name accepted");
        settings.Tags[1].Name = "Hero"; Check(module.SaveSettings(settings, 2, error), error);
        auto* scene = runtime->CreateScene("Test"); auto* object = scene->CreateGameObject("Tagged"); Check(object->SetTagID(1), "Tag assignment failed");
        auto deleted = settings; deleted.Tags.erase(deleted.Tags.begin() + 1);
        Check(!module.SaveSettings(deleted, 3, error) && module.GetRevision() == 3, "Live Tag removed");
        Check(scene->DestroyGameObject(object), "Cannot destroy tagged object");
        Write(root.filePath("Assets/Closed.scene"), QByteArray("{\"Objects\":[{\"TagID\":1}]}"));
        Check(!module.SaveSettings(deleted, 3, error), "Closed Scene Tag removed");
        Check(QFile::remove(root.filePath("Assets/Closed.scene")), "Cannot remove scene fixture");
        Write(root.filePath("Assets/Closed.prefab"), QByteArray("{\"Data\":{\"Annotations\":{\"Overrides\":[{\"Path\":[\"TagID\"],\"After\":1}]}}}"));
        Check(!module.SaveSettings(deleted, 3, error), "Saved Prefab override Tag removed");
        Check(QFile::remove(root.filePath("Assets/Closed.prefab")), "Cannot remove prefab fixture");
        scenes.ReferencedTags = {1};
        Check(!module.SaveSettings(deleted, 3, error) && module.GetRevision() == 3, "Tag referenced by an undo/document snapshot was removed");
        scenes.ReferencedTags.clear();
        Check(root.mkpath("Assets/Unreadable"), "Cannot create permission fixture");
        const auto inaccessible = root.filePath("Assets/Unreadable");
        struct RestorePermissions {
            QString Path; QFileDevice::Permissions Permissions;
            ~RestorePermissions() { QFile::setPermissions(Path, Permissions); }
        } permissionGuard{inaccessible, QFileInfo(inaccessible).permissions()};
        Check(QFile::setPermissions(inaccessible, {}), "Cannot restrict directory fixture");
        if (!QFileInfo(inaccessible).isReadable())
            Check(!module.SaveSettings(deleted, 3, error) && module.GetRevision() == 3, "Unreadable subtree was silently skipped");
        Check(QFile::setPermissions(inaccessible, permissionGuard.Permissions), "Cannot restore directory permissions");
        Check(root.rmdir("Assets/Unreadable"), "Cannot remove permission fixture");
        Check(root.rename("Assets", "SavedAssets"), "Cannot move Assets fixture");
        Write(root.filePath("Assets"), QByteArray("not a directory"));
        Check(!module.SaveSettings(deleted, 3, error) && module.GetRevision() == 3, "Invalid Assets file was accepted as an empty source directory");
        Check(QFile::remove(root.filePath("Assets")) && root.rename("SavedAssets", "Assets"), "Cannot restore Assets fixture");
        Check(module.SaveSettings(deleted, 3, error), error); settings = deleted;
        auto reused = settings; reused.Tags.push_back({1, "Different"});
        Check(!module.SaveSettings(reused, 4, error), "Deleted Tag identity reused");
        play.State.State = EditorPlayState::Playing;
        Check(!module.SaveSettings(settings, 4, error), "Settings edited during Play"); play.State.State = EditorPlayState::Edit;
        GameSproutRuntime::RuntimeProjectSettings decoded;
        Check(ProjectSettingsFormat::Decode(Read(path), decoded, error) && decoded == settings, error);
        Json::Value wrong; Check(ProjectSettingsFormat::Parse(Read(path), wrong, error), error);
        wrong["Version"] = 0; Json::StreamWriterBuilder writer;
        Check(!ProjectSettingsFormat::Decode(QByteArray::fromStdString(Json::writeString(writer, wrong)), decoded, error), "Old settings version accepted");
        Write(path, QByteArray("{}"));
        settings.LayerNames[4] = "World";
        Check(!module.SaveSettings(settings, 4, error) && Read(path) == "{}", "External file edit overwritten");
        module.UnregisterSettingsListener(&listener);
        GameSproutEditor::GameSproutEditorFinal::Release(module, api);
        Check(runtime->DestroyScene(scene), "Cannot destroy scene fixture");
        delete runtime; runtime = nullptr;
        std::cout << "PASS defaults, stable Tag IDs, rename, references, masks, events, revisions, Play and external conflicts\n"; return 0;
    } catch (const std::exception& exception) {
        delete runtime; std::cerr << "FAIL " << exception.what() << "\n"; return 1;
    }
}

#pragma once
#include <QFileInfo>

static void CheckSceneSessionRestoration() {
    QTemporaryDir temporary;
    Check(temporary.isValid(),"Cannot create recent-scene project fixture");
    const QDir workspace(temporary.path());
    const QDir firstRoot(workspace.filePath("ProjectA")),secondRoot(workspace.filePath("ProjectB"));
    Check(QDir().mkpath(firstRoot.filePath("Assets/Scenes"))&&QDir().mkpath(secondRoot.filePath("Assets/Scenes")),
        "Cannot create recent-scene project folders");
    const QString session=firstRoot.filePath("UserSettings/EditorState.json");
    const QString firstScene=firstRoot.filePath("Assets/Scenes/First.scene");
    const QString secondScene=firstRoot.filePath("Assets/Scenes/Second.scene");
    const QString thirdScene=firstRoot.filePath("Assets/Scenes/Third.scene");

    // A fresh Module + Runtime instance represents reopening the same project.
    // The fixture never depends on a process-global QSettings cache.
    const auto reopen=[&](const QDir& root,const std::function<void(SceneManager&)>& inspect) {
        Project project;project.Root=root.absolutePath().toStdString();Editor editor(project);
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        SceneManager manager;
        SceneServicesScope services(editor,manager,api);
        Check(services.Start(),"Recent-scene independent module startup failed");
        inspect(manager);
        services.Stop();
    };
    const auto remembered=[&] {
        Json::Value value;
        Check(AssetArtifactIO::Parse(Read(session).toStdString(),value)&&value.isObject()&&value["LastScenePath"].isString(),
            "Last-scene state was not saved as complete JSON");
        return value["LastScenePath"].asString();
    };
    const auto setRemembered=[&](const std::string& path) {
        Check(QDir().mkpath(firstRoot.filePath("UserSettings")),"Cannot create editor settings fixture");
        Json::Value state(Json::objectValue);state["LastScenePath"]=path;
        Write(session,QByteArray::fromStdString(AssetArtifactIO::JsonText(state)));
    };
    const auto checkDefault=[&](SceneManager& manager) {
        Check(manager.GetActiveScene()&&manager.GetActiveScenePath().empty()&&manager.GetActiveScene()->GetName()=="Untitled",
            "Invalid/missing recent scene did not fall back to the initial Untitled scene");
        Check(manager.HasUnsavedChanges()&&!manager.CanUndo()&&!manager.CanRedo(),
            "Default fallback inherited the old scene baseline or undo history");
    };

    QByteArray savedFirst,savedSecond;
    reopen(firstRoot,[&](SceneManager& manager) {
        checkDefault(manager);
        Check(manager.GetActiveScene()->CreateGameObject("Saved First"),"Cannot create session persistence marker");
        std::string error;
        Check(manager.SaveSceneAs("Scenes/First.scene",error),error);
        savedFirst=Read(firstScene);
        Check(remembered()=="Scenes/First.scene","First Save As did not immediately record its relative scene path");
        Check(manager.GetActiveScene()->CreateGameObject("Saved Second"),"Cannot create second scene marker");
        Check(manager.SaveSceneAs("Scenes/Second.scene",error),error);
        savedSecond=Read(secondScene);
        Check(remembered()=="Scenes/Second.scene"&&Read(firstScene)==savedFirst,
            "Save As did not update the recent scene or modified the previous source file");
        Check(manager.OpenScene("Scenes/First.scene",error),error);
        Check(remembered()=="Scenes/First.scene","Opening an existing scene did not record the last opened scene");
        Check(manager.GetActiveScene()->CreateGameObject("Unsaved Session Change"),"Cannot create unsaved session marker");
        Check(manager.HasUnsavedChanges(),"Unsaved session fixture did not become dirty");
        // Releasing the editor records which scene is open; it must not save edits.
    });
    Check(Read(firstScene)==savedFirst&&Read(secondScene)==savedSecond,
        "Leaving a project automatically saved scene data or changed another scene");
    const QStringList settingsFiles=QDir(firstRoot.filePath("UserSettings")).entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot);
    Check(settingsFiles==QStringList{"EditorState.json"},"Atomic recent-scene save left an unfinished settings file");

    reopen(firstRoot,[&](SceneManager& manager) {
        Check(manager.GetActiveScenePath()=="Scenes/First.scene"&&manager.GetActiveScene()->FindChild("Saved First")
            &&!manager.GetActiveScene()->FindChild("Saved Second")&&!manager.GetActiveScene()->FindChild("Unsaved Session Change")
            &&!manager.HasUnsavedChanges()&&!manager.CanUndo()&&!manager.CanRedo(),
            "Project reopen did not restore the saved last-opened scene without transient edits");
        std::string error;
        Check(manager.SaveSceneAs("Scenes/Third.scene",error)&&remembered()=="Scenes/Third.scene",error);
        const QByteArray settingsBefore=Read(session);
        Write(firstRoot.filePath("Assets/Scenes/Broken.scene"),"{ invalid scene JSON");
        Check(!manager.OpenScene("Scenes/Broken.scene",error)&&!manager.OpenScene("Scenes/Missing.scene",error)
            &&!manager.OpenScene("../Outside.scene",error)&&Read(session)==settingsBefore
            &&manager.GetActiveScenePath()=="Scenes/Third.scene",
            "Failed scene open replaced the last successful scene record");
        Check(!manager.SaveSceneAs("Scenes/Second.scene",error)&&!manager.SaveSceneAs("../Outside.scene",error)
            &&Read(session)==settingsBefore,"Failed Save As replaced the last successful scene record");
        const QByteArray externallyEdited=Read(thirdScene)+"\n  \n";
        Write(thirdScene,externallyEdited);
        Check(!manager.SaveScene(error)&&Read(session)==settingsBefore&&Read(thirdScene)==externallyEdited,
            "Failed conflicted save changed the recent scene record or external source bytes");
        Check(manager.OpenScene("Scenes/First.scene",error)&&remembered()=="Scenes/First.scene",error);
    });

    // The same editor executable must keep separate session records per project.
    reopen(secondRoot,[&](SceneManager& manager) {
        checkDefault(manager);
        std::string error;
        Check(manager.SaveSceneAs("Scenes/Other.scene",error),error);
    });
    reopen(firstRoot,[&](SceneManager& manager) {
        Check(manager.GetActiveScenePath()=="Scenes/First.scene"&&manager.GetActiveScene()->FindChild("Saved First"),
            "Opening another project replaced this project's last scene");
    });
    reopen(secondRoot,[&](SceneManager& manager) {
        Check(manager.GetActiveScenePath()=="Scenes/Other.scene"&&!manager.HasUnsavedChanges(),
            "Second project did not restore its own scene");
    });

    setRemembered("Scenes/First.scene");
    Check(QFile::remove(firstScene),"Cannot delete remembered scene fixture");
    reopen(firstRoot,checkDefault);
    Check(!QFileInfo::exists(firstScene),"Missing remembered scene was silently recreated");

    const QByteArray brokenBytes="{ invalid scene JSON";
    Write(firstRoot.filePath("Assets/Scenes/Broken.scene"),brokenBytes);
    setRemembered("Scenes/Broken.scene");
    reopen(firstRoot,checkDefault);
    Check(Read(firstRoot.filePath("Assets/Scenes/Broken.scene"))==brokenBytes,
        "Invalid remembered scene source was overwritten during startup fallback");

    // A valid scene exists outside Assets. Traversal/absolute records must not load it.
    const QString outside=firstRoot.filePath("Outside.scene");Write(outside,savedSecond);
    setRemembered("../Outside.scene");reopen(firstRoot,checkDefault);
    setRemembered(outside.toStdString());reopen(firstRoot,checkDefault);
    Check(Read(outside)==savedSecond,"Out-of-project session record changed external scene bytes");

    Write(session,"{ broken editor state");reopen(firstRoot,checkDefault);
    Write(session,"{\"LastScenePath\":42}");reopen(firstRoot,checkDefault);
    setRemembered("");reopen(firstRoot,checkDefault);
    std::cout<<"PASS per-project last scene: first launch, reopen, Save As, failures, isolation and safe fallback\n";
}

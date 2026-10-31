#include "SceneManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule& module,GameSproutModuleApi& api) {module.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutModule& module,GameSproutModuleApi& api) {return module.Initialize(&api);}
    static bool Launch(IGameSproutModule& module,GameSproutModuleApi& api) {return module.Launch(&api);}
    static void Update(IGameSproutModule& module,GameSproutModuleApi& api) {module.Update(&api);}
    static void Release(IGameSproutModule& module,GameSproutModuleApi& api) {module.Release(&api);}
};
}
static void Check(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
class Project final : public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override {return Root;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
};
class Editor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit Editor(Project& project):GameSproutViewApi(nullptr),_Project(project) {}
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {return name=="Project"?&_Project:nullptr;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&) const override {return nullptr;}
    Project& _Project;
};
class Listener final : public ISceneListener {
public:
    int Active=0,State=0;
    void OnActiveSceneChanged() override {++Active;}
    void OnSceneStateChanged() override {++State;}
};
static QByteArray Read(const QString& path) {QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read test scene");return file.readAll();}
static void Write(const QString& path,const QByteArray& bytes) {QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write external scene edit");Check(file.write(bytes)==bytes.size(),"External scene edit failed");}
static Json::Value Document(const QString& path) {Json::Value document;Check(AssetArtifactIO::Parse(Read(path).toStdString(),document),"Cannot parse test scene");return document;}
static const Json::Value& Find(const Json::Value& document,const std::string& name) {
    for(const auto& node:document["Objects"]) if(node["Name"].asString()==name)return node;
    throw std::runtime_error("Saved node missing: "+name);
}
#include "SceneTransformChecks.h"
#include "SceneDeletionChecks.h"
#include "SceneAnimationEditingChecks.h"
#include "SceneRootMotionEditingChecks.h"

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    SceneManager manager;
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Temporary project unavailable");
        QDir root(temporary.path());Check(root.mkpath("Assets/Scenes"),"Cannot create Assets");
        Project project;project.Root=temporary.path().toStdString();Editor editor(project);
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        GameSproutEditor::GameSproutEditorFinal::Before(manager,api);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(manager,api),"SceneManager SDK startup failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(manager,api),"Default scene failed");
        Listener listener;Check(manager.RegisterSceneListener(&listener),"Cannot subscribe scene state listener");
        std::string error;
        Check(manager.HasUnsavedChanges() && !manager.SaveScene(error),"Untitled must require first Save As");
        Check(manager.SaveSceneAs("Scenes/One.scene",error),error);
        const QString filename=root.filePath("Assets/Scenes/One.scene");
        Check(!manager.HasUnsavedChanges() && manager.GetActiveScenePath()=="Scenes/One.scene","Save As did not establish baseline");
        const int statesAfterSave=listener.State;
        GameSproutEditor::GameSproutEditorFinal::Update(manager,api);
        Check(!manager.HasUnsavedChanges() && listener.State==statesAfterSave,"Late Runtime events marked just-saved scene dirty/notified again");
        auto* scene=manager.GetActiveScene();auto* camera=scene->GetMainCamera()->GetGameObject();
        const auto cameraID=camera->GetInstanceID();
        const std::string oldName=camera->GetName(),newName="Edited Camera";
        const auto rename=[newName](GameSproutRuntime::IGameObject& object,std::string&) {object.SetName(newName);return object.GetName()==newName;};
        const auto restore=[oldName](GameSproutRuntime::IGameObject& object,std::string&) {object.SetName(oldName);return object.GetName()==oldName;};
        Check(manager.EditObject(cameraID,"Rename",rename,restore,error) && manager.CanUndo() && manager.HasUnsavedChanges(),error);
        Check(manager.Undo(error) && camera->GetName()==oldName && !manager.HasUnsavedChanges() && manager.CanRedo(),"Undo did not return to saved baseline");
        Check(manager.Redo(error) && camera->GetName()==newName && manager.HasUnsavedChanges(),"Redo failed");
        Check(manager.SaveScene(error) && !manager.HasUnsavedChanges(),error);
        Check(manager.Undo(error) && manager.HasUnsavedChanges(),"Undo after Save should become dirty");
        Check(manager.Redo(error) && !manager.HasUnsavedChanges(),"Redo back to saved snapshot should become clean");
        const auto invalid=[](GameSproutRuntime::IGameObject& object,std::string&) {object.SetName("");return true;};
        const auto restoreEdited=[newName](GameSproutRuntime::IGameObject& object,std::string&) {object.SetName(newName);return true;};
        Check(!manager.EditObject(cameraID,"Invalid name",invalid,restoreEdited,error) && camera->GetName()==newName &&
              !manager.HasUnsavedChanges(),"Invalid edit was not rolled back");
        Check(manager.Undo(error) && camera->GetName()==oldName,"Failed edit corrupted Undo history");
        Check(manager.Redo(error),error);
        auto* parent=scene->CreateGameObject("Parent");Check(parent->AddChild(camera,false),"Cannot create overflow test parent");
        parent->GetTransform()->SetLocalScale({std::numeric_limits<float>::max(),1,1});
        const auto overflow=[](GameSproutRuntime::IGameObject& object,std::string&) {object.GetTransform()->SetLocalPosition({std::numeric_limits<float>::max(),0,3});return true;};
        const auto restorePosition=[](GameSproutRuntime::IGameObject& object,std::string&) {object.GetTransform()->SetLocalPosition({0,0,3});return true;};
        Check(!manager.EditObject(cameraID,"Overflow",overflow,restorePosition,error) && camera->GetTransform()->GetLocalPosition().X==0,"World overflow edit was accepted or not reverted");
        parent->GetTransform()->SetLocalScale({1,1,1});
        camera->GetTransform()->SetLocalPosition({2,3,4});
        Check(manager.HasUnsavedChanges(),"Undispatched direct setter was not detected");
        Check(manager.SaveScene(error),error);
        auto saved=Document(filename);const auto stableCameraID=Find(saved,newName)["ID"].asUInt64();
        Check(Find(saved,newName)["Components"][0]["Position"][0].asFloat()==2,"Save omitted an undispatched setter");
        bool guarded=false;
        scene->ForeachChild([&](GameSproutRuntime::IGameObject*) {
            if(guarded)return;guarded=true;
            Check(!manager.EditObject(cameraID,"Callback edit",rename,restore,error) && !manager.Undo(error) &&
                  !manager.SaveScene(error) && !manager.OpenScene("Scenes/One.scene",error),"Runtime callback allowed scene mutation/save");
        });
        Check(guarded,"Traversal guard was not exercised");
        QFile other(root.filePath("Assets/Scenes/Other.scene"));Check(other.open(QIODevice::WriteOnly),"Cannot create foreign scene");other.write("foreign bytes");other.close();
        Check(!manager.SaveSceneAs("Scenes/Other.scene",error) && Read(other.fileName())=="foreign bytes" && manager.GetActiveScenePath()=="Scenes/One.scene","Save As overwrote another existing scene");
        Check(!manager.SaveSceneAs("../Outside.scene",error) && !manager.SaveSceneAs(filename.toStdString(),error) &&
              !manager.SaveSceneAs("Scenes/Missing/New.scene",error),"Invalid/outside Save As path accepted");
        QTemporaryDir outside;Check(outside.isValid(),"Cannot create symlink fixture");
        const QString link=root.filePath("Assets/Linked");
        if(QFile::link(outside.path(),link)) Check(!manager.SaveSceneAs("Linked/Outside.scene",error),"Symbolic-link Save As accepted");
        const QByteArray beforeFailure=Read(filename);
        const auto permissions=QFile::permissions(root.filePath("Assets/Scenes"));
        QFile::setPermissions(root.filePath("Assets/Scenes"),QFileDevice::ReadOwner|QFileDevice::ExeOwner|QFileDevice::ReadGroup|QFileDevice::ExeGroup|QFileDevice::ReadOther|QFileDevice::ExeOther);
        camera->SetName("Cannot persist");const bool writeSucceeded=manager.SaveScene(error);
        QFile::setPermissions(root.filePath("Assets/Scenes"),permissions);
        if(!writeSucceeded) Check(Read(filename)==beforeFailure && manager.HasUnsavedChanges(),"Failed atomic save changed existing bytes/baseline");
        else std::cout<<"SKIP directory write-denial check: platform permits directory writes\n";
        camera->SetName(newName);
        Check(manager.SaveScene(error),error);
        Check(manager.OpenScene("Scenes/One.scene",error),error);
        Check(!manager.CanUndo() && !manager.CanRedo() && !manager.HasUnsavedChanges(),"Open did not clear history/baseline");
        scene=manager.GetActiveScene();camera=scene->FindChild("Parent")->FindChild(newName);
        Check(camera && camera->GetInstanceID()!=cameraID,"Open reused Runtime identity");
        Check(manager.SaveScene(error) && Find(Document(filename),newName)["ID"].asUInt64()==stableCameraID,"Open/save changed stable file identity");

        // 只改 JSON 空白也应被原始字节基线识别，不能用规范化 Runtime 快照检测磁盘冲突。
        camera->SetName("Local preserved");
        const auto localID=camera->GetInstanceID();
        const QByteArray external=Read(filename)+"\n  \n";
        Write(filename,external);
        Check(!manager.SaveScene(error) && !manager.SaveSceneAs("Scenes/One.scene",error) &&
              !error.empty() && Read(filename)==external && camera->GetInstanceID()==localID &&
              camera->GetName()=="Local preserved" && manager.GetActiveScenePath()=="Scenes/One.scene" &&
              manager.HasUnsavedChanges(),"External byte changes were overwritten or local scene was lost");
        Check(manager.SaveSceneAs("Scenes/Local.scene",error),error);
        const QString localFile=root.filePath("Assets/Scenes/Local.scene");
        Check(Find(Document(localFile),"Local preserved")["ID"].asUInt64()==stableCameraID &&
              Read(filename)==external && !manager.HasUnsavedChanges(),"Save As did not preserve local data and external original");
        Check(manager.OpenScene("Scenes/One.scene",error),error);
        Check(manager.SaveScene(error) && manager.SaveScene(error),"Reopen/raw-save baselines did not allow a subsequent valid save: "+error);
        scene=manager.GetActiveScene();camera=scene->FindChild("Parent")->FindChild(newName);
        Check(camera && QFile::remove(filename),"Cannot delete external-save fixture");
        const auto preservedID=camera->GetInstanceID();
        Check(!manager.SaveScene(error) && !manager.SaveSceneAs("Scenes/One.scene",error) && !QFileInfo::exists(filename) &&
              camera->GetInstanceID()==preservedID && camera->GetName()==newName && manager.GetActiveScenePath()=="Scenes/One.scene",
              "Save recreated an externally deleted current scene or changed local state");
        Check(manager.SaveSceneAs("Scenes/Recovered.scene",error) && manager.SaveScene(error),"Cannot recover local scene to a new path: "+error);
        const auto deletedID=camera->GetInstanceID();
        Check(manager.EditObject(deletedID,"Deleted target",rename,restore,error),error);
        Check(scene->DestroyGameObject(camera),"Cannot remove Undo target");
        Check(!manager.Undo(error) && !error.empty(),"Undo used a deleted target");
        CheckTransformTransactions(manager,api,root);
        CheckSceneDeletion(manager,api,root);
        CheckAnimationSceneDeletion(manager,api,root);
        CheckRootMotionEditing(manager,root);
        manager.UnregisterSceneListener(&listener);
        GameSproutEditor::GameSproutEditorFinal::Release(manager,api);
        std::cout<<"PASS SceneManager: stable IDs, snapshot dirty state, Undo/Redo/rollback, guarded callbacks, atomic safe-path Save/Open\n";
        return 0;
    } catch(const std::exception& exception) {std::cerr<<"FAIL "<<exception.what()<<'\n';return 1;}
}

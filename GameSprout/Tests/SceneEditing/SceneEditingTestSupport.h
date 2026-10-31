#pragma once
#include "../AssetsImport/ProjectViewTestSupport.h"
#include "ISceneManager.h"
#include "ISelectionManager.h"
#include "ILibraryManager.h"
#include <QElapsedTimer>
#include <limits>

class SceneEditingEmptyAssets final : public IAssetsManager {
public:
    IAssetMeta* QueryAsset(const Guid&) const override{return nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&, std::string&, std::string&) const override { return false; }
    bool SaveMaterialSource(const Guid&, const std::string&, const std::string&, std::string&) override { return false; }
    bool ExtractMaterial(const Guid&, uint64_t, const std::string&, std::string&, std::string&) override { return false; }
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return {};}
    bool RegisterAssetListener(IAssetListener* value) override{return value!=nullptr;}
    void UnregisterAssetListener(IAssetListener*) override{}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class SceneEditingEmptyLibrary final : public ILibraryManager {
public:
    std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest& output) const override{output={};return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid&,uint64_t) override{return nullptr;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class SceneEditingEditor final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit SceneEditingEditor(GRHI::GRHIApi* rhi):GameSproutViewApi(rhi){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    IIDEWindow* IDE=nullptr;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override {
        if(name=="IDEWindow")return IDE;
        const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;
    }
};
class SceneEditingServices final {
public:
    explicit SceneEditingServices(GRHI::GRHIApi* rhi=nullptr):Editor(rhi),ModuleAPI(nullptr,&Editor){}
    ~SceneEditingServices(){Shutdown();}
    void Initialize() {
        Check(Temporary.isValid(),"Cannot create Scene editing test project");
        Check(QDir(Temporary.path()).mkpath("Assets/Scenes"),"Cannot create scene folder");
        Project.Root=Temporary.path().toStdString();Library.Root=QDir(Temporary.path()).filePath("Library").toStdString();
        Editor.Modules["Project"]=&Project;Editor.Modules["AssetsManager"]=&Assets;Editor.Modules["LibraryManager"]=&Library;Editor.Modules["Console"]=&Console;
        Scenes=static_cast<ISceneManager*>(LoadModule("SceneManager"));Editor.Modules["SceneManager"]=Scenes;
        Selection=static_cast<ISelectionManager*>(LoadModule("SelectionManager"));Editor.Modules["SelectionManager"]=Selection;
        GameSproutEditor::GameSproutEditorFinal::Before(Scenes,&ModuleAPI);GameSproutEditor::GameSproutEditorFinal::Before(Selection,&ModuleAPI);
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(Scenes,&ModuleAPI),"Real SceneManager initialization failed");SceneReady=true;
        Check(GameSproutEditor::GameSproutEditorFinal::Initialize(Selection,&ModuleAPI),"Real SelectionManager initialization failed");SelectionReady=true;
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(Scenes,&ModuleAPI),"Real SceneManager launch failed");
        Check(GameSproutEditor::GameSproutEditorFinal::Launch(Selection,&ModuleAPI),"Real SelectionManager launch failed");Tick();
    }
    void Tick() {
        QCoreApplication::processEvents();
        if(SceneReady)GameSproutEditor::GameSproutEditorFinal::Update(Scenes,&ModuleAPI);
        if(SelectionReady)GameSproutEditor::GameSproutEditorFinal::Update(Selection,&ModuleAPI);
    }
    void Shutdown() {
        if(SelectionReady){GameSproutEditor::GameSproutEditorFinal::Release(Selection,&ModuleAPI);SelectionReady=false;}
        if(SceneReady){GameSproutEditor::GameSproutEditorFinal::Release(Scenes,&ModuleAPI);SceneReady=false;}
    }
    QByteArray ReadScene(const QString& name="Transaction.scene") const {
        QFile file(QDir(Temporary.path()).filePath("Assets/Scenes/"+name));Check(file.open(QIODevice::ReadOnly),"Cannot read saved scene");return file.readAll();
    }
    SceneEditingEditor Editor;
    GameSproutEditor::GameSproutModuleApi ModuleAPI;
    QTemporaryDir Temporary;
    TestProject Project;
    TestConsole Console;
    SceneEditingEmptyAssets Assets;
    SceneEditingEmptyLibrary Library;
    ISceneManager* Scenes=nullptr;
    ISelectionManager* Selection=nullptr;
    bool SceneReady=false,SelectionReady=false;
};
static SceneTransformState ReadTransform(GameSproutRuntime::IGameObject& object) {
    const auto* transform=object.GetTransform();return {transform->GetLocalPosition(),transform->GetLocalRotation(),transform->GetLocalScale()};
}
static bool CloseVector(GameSproutRuntime::Vector3 first,GameSproutRuntime::Vector3 second,float tolerance=.0001f) {
    return (first-second).Length()<=tolerance;
}
static bool CloseRotation(GameSproutRuntime::Quaternion first,GameSproutRuntime::Quaternion second,float tolerance=.0001f) {
    const float sign=first.X*second.X+first.Y*second.Y+first.Z*second.Z+first.W*second.W<0?-1.f:1.f;
    return std::fabs(first.X-second.X*sign)<=tolerance&&std::fabs(first.Y-second.Y*sign)<=tolerance&&std::fabs(first.Z-second.Z*sign)<=tolerance&&std::fabs(first.W-second.W*sign)<=tolerance;
}
static bool CloseTransform(const SceneTransformState& first,const SceneTransformState& second) {
    return CloseVector(first.Position,second.Position)&&CloseRotation(first.Rotation,second.Rotation)&&CloseVector(first.Scale,second.Scale);
}

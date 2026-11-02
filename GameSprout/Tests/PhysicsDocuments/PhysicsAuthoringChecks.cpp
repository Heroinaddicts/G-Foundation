#include "PhysicsAuthoring.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProject.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <map>
#include <iostream>
#include <stdexcept>
namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static void Before(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);}
    static bool Initialize(IGameSproutModule& module,GameSproutModuleApi& api){return module.Initialize(&api);}
    static void Release(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class T>class Module:public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Project final:public Module<IProject> {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override{return Root;}
};
class Meta final:public IAssetMeta {
public:
    Meta():IAssetMeta(eAssetImporterType::PhysicsMaterial,Guid("1234567890abcdef1234567890abcdef")){}
    std::string Path;
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return eAssetImportState::Failed;}
    std::string GetImportError() const override{return "Needs editing";}
    bool SerializeMeta(std::string&) const override{return false;}
    bool SetSubAssets(const std::string&) override{return false;}
    bool SaveMeta() override{return false;}
};
class Assets final:public Module<IAssetsManager> {
public:
    Meta Material;unsigned Imports=0;
    IAssetMeta* QueryAsset(const Guid& guid) const override{return guid.GetGuidString()==Material._Guid.GetGuidString()?const_cast<Meta*>(&Material):nullptr;}
    bool ReimportAsset(const Guid&) override{++Imports;return true;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {const_cast<Meta*>(&Material)};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return GetAssetMetas();}
    bool RegisterAssetListener(IAssetListener*) override{return false;}
    void UnregisterAssetListener(IAssetListener*) override{}
};
class Library final:public Module<ILibraryManager> {
public:
    std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&) const override{return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid&,uint64_t) override{return nullptr;}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    Editor():GameSproutViewApi(nullptr){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&) const override{return nullptr;}
};
static QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read fixture");return file.readAll();}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create project");QDir root(temporary.path());Check(root.mkpath("Assets/Surfaces"),"Cannot create Assets");
        Project project;project.Root=temporary.path().toStdString();Assets assets;Library library;Editor editor;
        editor.Modules={{"Project",&project},{"AssetsManager",&assets},{"LibraryManager",&library}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);PhysicsAuthoring module;
        GameSproutEditor::GameSproutEditorFinal::Before(module,api);Check(GameSproutEditor::GameSproutEditorFinal::Initialize(module,api),"Cannot initialize authoring module");
        std::string path,error;Check(module.CreatePhysicsMaterialAsset("Surfaces","Ice",path,error),error);
        Check(path=="Surfaces/Ice.physicmaterial","Wrong source path");const QString filename=root.filePath("Assets/"+QString::fromStdString(path));const QByteArray original=Read(filename);
        std::string ignored;Check(!module.CreatePhysicsMaterialAsset("Surfaces","Ice",ignored,error)&&Read(filename)==original,"Create overwrote existing source");
        Check(!module.CreatePhysicsMaterialAsset("../Outside","Ice",ignored,error),"Source escaped Assets");
        assets.Material.Path=path;GameSproutRuntime::ResourceReference reference{assets.Material._Guid.GetGuidString(),0};
        GameSproutRuntime::PhysicsMaterialData data;std::string snapshot;
        Check(module.LoadPhysicsMaterialSource(reference,data,snapshot,error),"Failed imported source could not be edited: "+error);
        data.Friction=0.1f;data.Restitution=0.5f;data.FrictionCombine=GameSproutRuntime::PhysicsMaterialCombine::Minimum;
        Check(module.SavePhysicsMaterialSource(reference,data,snapshot,error)&&assets.Imports==1,error);
        const QByteArray saved=Read(filename);Check(saved!=original,"Save did not update source");
        data.Friction=0.8f;Check(!module.SavePhysicsMaterialSource(reference,data,snapshot,error)&&Read(filename)==saved&&assets.Imports==1,"Stale source snapshot overwrote external/current changes");
        Check(module.LoadPhysicsMaterialSource(reference,data,snapshot,error)&&data.Friction==0.1f,error);
        data.Restitution=2;Check(!module.SavePhysicsMaterialSource(reference,data,snapshot,error)&&Read(filename)==saved,"Invalid surface persisted");
        reference.LocalID=987;Check(!module.LoadPhysicsMaterialSource(reference,data,snapshot,error),"Wrong prepared child identity accepted");
        Check(module.CreatePhysicsMaterialAsset("Surfaces","Other",path,error),error);QFile meta(root.filePath("Assets/Surfaces/Blocked.physicmaterial.meta"));Check(meta.open(QIODevice::WriteOnly),"Cannot make orphan meta");meta.write("{}");meta.close();
        Check(!module.CreatePhysicsMaterialAsset("Surfaces","Blocked",ignored,error)&&!QFile::exists(root.filePath("Assets/Surfaces/Blocked.physicmaterial")),"Source overwrote an orphan meta identity");
        GameSproutEditor::GameSproutEditorFinal::Release(module,api);
        std::cout<<"PASS physical material source creation, failed-import editing, conflict checks and safe paths\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<"\n";return 1;}
}

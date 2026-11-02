#include "ShaderAuthoring.h"
#include "ShaderUI.h"
#include "IProject.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "IProjectAssetsUI.h"
#include "../../Module/Shared/MaterialDefinition.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <map>
#include <stdexcept>

namespace GameSproutEditor {
class GameSproutEditorFinal {
public:
    static bool Start(IGameSproutModule& module,GameSproutModuleApi& api){module.BeforeInitialize(&api);return module.Initialize(&api)&&module.Launch(&api);}
    static void Stop(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
    static bool Start(IGameSproutView& view,GameSproutViewApi& api){view.BeforeInitialize(&api);return view.Initialize(&api)&&view.Launch(&api);}
    static void Stop(IGameSproutView& view,GameSproutViewApi& api){view.Release(&api);}
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
    Meta():IAssetMeta(eAssetImporterType::Shader,Guid("1234567890abcdef1234567890abcdef")){}
    std::string Path="Surface.shader";eAssetImportState State=eAssetImportState::Ready;
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return State;}
    std::string GetImportError() const override{return {};}
    bool SerializeMeta(std::string&) const override{return false;}
    bool SetSubAssets(const std::string&) override{return false;}
    bool SaveMeta() override{return false;}
};
class Assets final:public Module<IAssetsManager> {
public:
    Meta Shader;
    IAssetMeta* QueryAsset(const Guid& guid) const override{return guid==Shader._Guid?const_cast<Meta*>(&Shader):nullptr;}
    bool ReimportAsset(const Guid&) override{return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{return {const_cast<Meta*>(&Shader)};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return GetAssetMetas();}
    bool RegisterAssetListener(IAssetListener*) override{return true;}
    void UnregisterAssetListener(IAssetListener*) override{}
};
class Asset final:public IAsset {
public:
    Guid ID=Guid("1234567890abcdef1234567890abcdef");std::string Text,Name="Surface",Path;
    const Guid& GetGuid() const override{return ID;}
    uint64_t GetLocalID() const override{return 77;}
    eAssetType GetAssetType() const override{return eAssetType::Shader;}
    const std::string& GetName() const override{return Name;}
    const std::string& GetArtifactPath() const override{return Path;}
    const std::string& GetDataDescriptorJson() const override{return Text;}
    uint32_t GetDataVersion() const override{return 2;}
    uint64_t GetDataSize() const override{return 0;}
};
class Library final:public Module<ILibraryManager> {
public:
    Asset Shader;std::string Root;
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {};}
    bool GetManifest(const Guid& guid,AssetImportManifest& manifest) const override {
        if(guid!=Shader.ID)return false;manifest={};AssetArtifact artifact;artifact.Type=eAssetType::Shader;artifact.LocalID=77;manifest.Assets.push_back(artifact);return true;
    }
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid& guid,uint64_t localID) override{return guid==Shader.ID&&localID==77?&Shader:nullptr;}
};
class ProjectAssets final:public IProjectAssetsUI {
public:
    ProjectAssetActionContext Context;std::string Created;bool Unregistered=false;
    std::map<std::vector<std::string>,fContextActionCallback> Actions;
    std::vector<ProjectAssetSelection> GetSelection() const override{return Context.Selection;}
    bool NavigateToDirectory(const std::string&) override{return true;}
    bool RegisterAssetOpenHandler(const void*,const fAssetOpenHandler&) override{return false;}
    bool RegisterContextAction(const void*,const std::vector<std::string>& path,fContextActionCallback callback,const fContextActionEnabled&) override{return Actions.emplace(path,callback).second;}
    void UnregisterExtensions(const void*) override{Unregistered=true;Actions.clear();}
    ProjectAssetActionContext GetActionContext() const override{return Context;}
    void NotifyAssetCreated(const std::string& path) override{Created=path;}
private:
    bool Initialize(GameSproutEditor::GameSproutViewApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutViewApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutViewApi*) override{}
};
class Editor final:public GameSproutEditor::GameSproutViewApi {
public:
    Editor():GameSproutViewApi(nullptr){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    std::map<std::string,GameSproutEditor::IGameSproutView*> Views;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override{const auto found=Views.find(name);return found==Views.end()?nullptr:found->second;}
};
static QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read source");return file.readAll();}
static void Touch(const QString& path){QFile file(path);Check(file.open(QIODevice::WriteOnly),"Cannot create fixture");file.write("reserved");}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        QTemporaryDir temporary;Check(temporary.isValid(),"Cannot create isolated fixture");
        Project project;project.Root=temporary.path().toStdString();QDir(temporary.path()).mkpath("Assets/Materials");
        Assets assets;Library library;ProjectAssets browser;ShaderAuthoring authoring;ShaderUI view;Editor editor;
        editor.Modules={{"Project",&project},{"AssetsManager",&assets},{"LibraryManager",&library},{"ShaderAuthoring",&authoring}};
        editor.Views={{"ProjectAssetsUI",&browser}};GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        Check(GameSproutEditor::GameSproutEditorFinal::Start(authoring,api),"Cannot start authoring");
        std::string path,error;
        Check(authoring.CreateUnlitShader("Materials","Surface",path,error),error);
        Check(path=="Materials/Surface.shader"&&!QFileInfo::exists(temporary.path()+"/Assets/"+QString::fromStdString(path)+".meta"),"Authoring stole Meta ownership");
        const auto source=Read(temporary.path()+"/Assets/"+QString::fromStdString(path));
        Check(source.contains("GameSproutShader")&&source.contains("VSMain")&&source.contains("PSMain"),"Incomplete shader template");
        Check(!authoring.CreateUnlitShader("Materials","surface",path,error),"Case-insensitive source overwrite accepted");
        Check(Read(temporary.path()+"/Assets/Materials/Surface.shader")==source,"Duplicate creation changed existing source");
        Touch(temporary.path()+"/Assets/Materials/Reserved.shader.meta");
        Check(!authoring.CreateUnlitShader("Materials","Reserved",path,error)&&!QFileInfo::exists(temporary.path()+"/Assets/Materials/Reserved.shader"),"Meta reservation was overwritten");
        Check(!authoring.CreateUnlitShader("../","Outside",path,error)&&!authoring.CreateUnlitShader("","../Outside",path,error),"Path traversal accepted");
        Check(authoring.CreateMaterial("Materials","Default",{},path,error),error);Json::Value material;
        Check(AssetArtifactIO::Parse(Read(temporary.path()+"/Assets/Materials/Default.mat").toStdString(),material)&&material["Shader"]=="GameSprout/Preview","Default material shader mismatch");
        Check(authoring.CreateBuiltinMaterial("Materials","Lit",BuiltinMaterialPreset::Lit,path,error),error);
        Check(AssetArtifactIO::Parse(Read(temporary.path()+"/Assets/Materials/Lit.mat").toStdString(),material)
            &&material["Shader"]=="GameSprout/Lit"&&material["Roughness"].asDouble()==.5
            &&material["NormalTexture"].isNull()&&material["NormalTextureST"][0].asInt()==1&&material["Emission"][0].asInt()==0,"Lit preset defaults missing");
        Check(material["EnableGPUInstancing"]==true&&material["SortingPriority"]==0&&material["SpecularHighlights"]==true,
            "Lit preset render options missing");
        auto invalid=material;invalid["NormalScale"]=-1;
        Check(!MaterialDefinition::ValidateValues(MaterialDefinition::LitSchema(),invalid,error),"Negative normal scale accepted");
        invalid=material;invalid["BaseColor"][0]=2;
        Check(!MaterialDefinition::ValidateValues(MaterialDefinition::LitSchema(),invalid,error),"Non-HDR Lit base color overflow accepted");
        Json::Value schema;Check(MaterialDefinition::ParseShaderSchema(source,schema,error),error);
        Json::Value descriptor;descriptor["Material"]=schema;library.Shader.Text=AssetArtifactIO::JsonText(descriptor);
        const auto selected=GameSproutRuntime::ResourceReference{assets.Shader._Guid.GetGuidString(),0};
        Check(authoring.CreateMaterial("Materials","Custom",selected,path,error),error);
        Check(AssetArtifactIO::Parse(Read(temporary.path()+"/Assets/Materials/Custom.mat").toStdString(),material)&&material["Shader"]["LocalID"].asUInt64()==77&&material["Tint"][0].asInt()==1&&material["BaseTextureST"][0].asInt()==1,"Shader source selection/defaults were not resolved");
        Check(!authoring.CreateMaterial("Materials","Bad",{selected.Guid,99},path,error),"Wrong shader resource accepted");
        assets.Shader.State=eAssetImportState::Failed;
        Check(!authoring.CreateMaterial("Materials","Failed",selected,path,error),"Failed shader accepted");assets.Shader.State=eAssetImportState::Ready;
        Check(GameSproutEditor::GameSproutEditorFinal::Start(view,editor),"Cannot start Shader UI");
        Check(browser.Actions.size()==2&&browser.Actions.contains({"Create","Shader","Unlit"})&&browser.Actions.contains({"Create","Material"}),"Designer menus were not registered");
        browser.Context.Directory="Materials";browser.Actions.at({"Create","Shader","Unlit"})();QCoreApplication::processEvents();
        auto* dialog=app.activeModalWidget();Check(dialog,"Shader creation dialog did not open");
        auto* name=dialog->findChild<QLineEdit*>("nameEdit");auto* buttons=dialog->findChild<QDialogButtonBox*>("buttonBox");Check(name&&buttons,"Creation form missing Designer controls");
        name->setText("FromUI");QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QCoreApplication::processEvents();
        Check(browser.Created=="Materials/FromUI.shader"&&QFileInfo::exists(temporary.path()+"/Assets/Materials/FromUI.shader"),"UI did not route create result through resource browser");
        GameSproutEditor::GameSproutEditorFinal::Stop(view,editor);Check(browser.Unregistered,"View did not unregister extensions");
        GameSproutEditor::GameSproutEditorFinal::Stop(authoring,api);
        std::cout<<"PASS shader/material creation, no overwrite, defaults, identity and Designer menu routing\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

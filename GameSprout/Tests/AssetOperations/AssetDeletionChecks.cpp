#include "AssetsManager.h"
#include "AssetOperations.h"
#include "LibraryRuntimeResources.h"
#include "IProjectAssetsUI.h"
#include "IAssetOperationsUI.h"
#include "ISceneManager.h"
#include "IPrefabStage.h"
#include "IPlayMode.h"
#include "IRuntimeHost.h"
#include "GameSproutRuntimeExport.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>
#include <QUuid>
#include <iostream>
#include <map>
#include <stdexcept>
#include <algorithm>

IProjectAssetsUI* CreateDeletionBrowser(IAssetsManager*);
QWidget* DeletionBrowserWidget(IProjectAssetsUI*);
bool SelectDeletionSource(IProjectAssetsUI*,const std::string&);
IAssetOperationsUI* CreateDeletionView(GameSproutEditor::GameSproutViewApi*);
QDialog* DeletionDialog(IAssetOperationsUI*);
static void Check(bool result,const std::string& message){if(!result)throw std::runtime_error(message);}
static void Write(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);Check(file.open(QIODevice::WriteOnly),"Cannot create isolated file");Check(file.write(bytes)==bytes.size(),"Cannot write isolated file");}
template<class T>class Module:public T {
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class Meta final:public IAssetMeta {
public:
    Meta(const std::string& guid,std::string path,eAssetImporterType type=eAssetImporterType::Mesh):IAssetMeta(type,Guid(guid)),Path(std::move(path)){}
    std::string Path;
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings()const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets()const override{return {};}
    eAssetImportState GetImportState()const override{return eAssetImportState::Ready;}
    std::string GetImportError()const override{return {};}
    bool SerializeMeta(std::string& json)const override{json="{}";return true;}
    bool SetSubAssets(const std::string&)override{return true;}bool SaveMeta()override{return true;}
};
class Pipeline final:public Module<IAssetsPipeline> {
public:
    std::vector<std::string> Invalidated;
    void ImportAsset(IAssetMeta*,const fImportCallback&,bool) override{}
    void Invalidate(const Guid& guid)override{Invalidated.push_back(guid.GetGuidString());}
};
class Library final:public Module<ILibraryManager> {
public:
    std::string Root;std::map<std::string,std::vector<uint8_t>> Rows;std::vector<std::string> Removed;
    ILDBSession* GetLDBSession()const override{return nullptr;}
    const std::string& GetLibraryRoot()const override{return Root;}
    std::vector<std::string> GetManifestGuids()const override{return {};}
    bool GetManifest(const Guid&,AssetImportManifest&)const override{return false;}
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&)override{return false;}
    bool RemoveAsset(const Guid& guid,std::string&,const std::vector<LDBWriteOperation>& writes)override{
        Removed.push_back(guid.GetGuidString());
        for(const auto& write:writes){const auto key=write.Table+"/"+write.Key;if(write.Delete)Rows.erase(key);else Rows[key]=write.Data;}
        return true;
    }
    std::vector<std::string> FindDependents(const std::string&)const override{return {};}
    IAsset* QueryAsset(const Guid&,uint64_t)override{return nullptr;}
};
class Scenes final:public Module<ISceneManager> {
public:
    std::string Path,MainPath;GameSproutRuntime::IScene* Scene=nullptr;
    GameSproutRuntime::IScene* GetActiveScene()const override{return Scene;}
    const std::string& GetActiveScenePath()const override{return Path;}
    std::string GetMainScenePath()const override{return MainPath.empty()?Path:MainPath;}
    bool OpenScene(const std::string&,std::string&)override{return false;}
    bool EditObject(uint64_t,const std::string&,const fSceneEditOperation&,const fSceneEditOperation&,std::string&)override{return false;}
    bool Undo(std::string&)override{return false;}bool Redo(std::string&)override{return false;}
    bool CanUndo()const override{return false;}bool CanRedo()const override{return false;}
    bool SaveScene(std::string&)override{return false;}bool SaveSceneAs(const std::string&,std::string&)override{return false;}
    bool HasUnsavedChanges()const override{return false;}
    bool CreateSceneAsset(const std::string&,const std::string&,std::string&,std::string&)override{return false;}
    bool RegisterSceneListener(ISceneListener*)override{return true;}void UnregisterSceneListener(ISceneListener*)override{}
};
class Stage final:public Module<IPrefabStage>{
public:PrefabStageState State;
    PrefabStageState GetState()const override{return State;}
    bool OpenPrefab(const std::string&,std::string&)override{return false;}bool Save(std::string&)override{return false;}
    bool Close(bool,std::string&)override{return false;}bool RegisterStageListener(IPrefabStageListener*)override{return true;}
    void UnregisterStageListener(IPrefabStageListener*)override{}
};
class TestPlay final:public Module<IPlayMode>{
public:PlayModeState State;
    PlayModeState GetState()const override{return State;}
    bool Play(std::string&)override{return false;}bool Pause(std::string&)override{return false;}bool Resume(std::string&)override{return false;}
    bool Step(std::string&)override{return false;}bool Stop(std::string&)override{return false;}
    bool RegisterPlayModeListener(IPlayModeListener*)override{return true;}void UnregisterPlayModeListener(IPlayModeListener*)override{}
};
class Host final:public Module<IRuntimeHost>{
public:bool Frame=false;
    void SetSimulationPlaying(bool)override{}bool IsSimulationPlaying()const override{return false;}
    bool IsFrameActive()const override{return Frame;}void ResetClock()override{}
    bool RegisterFrameListener(IRuntimeFrameListener*)override{return true;}void UnregisterFrameListener(IRuntimeFrameListener*)override{}
};
class Selection final:public Module<ISelectionManager>{
public:std::vector<ProjectAssetSelection> Value;
    std::vector<ProjectAssetSelection> GetAssetSelection()const override{return Value;}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>& value)override{Value=value;}
    SceneObjectSelection GetSceneObjectSelection()const override{return {};}
    void SetSceneObjectSelection(const SceneObjectSelection&)override{}
    bool RegisterSelectionListener(ISelectionListener*)override{return true;}void UnregisterSelectionListener(ISelectionListener*)override{}
};
class Events final:public IAssetListener{
public:std::vector<std::string> Removed;
    void OnAssetCreate(const AssetEvent&)override{}void OnAssetModify(const AssetEvent&)override{}
    void OnAssetRemove(const AssetEvent& event)override{Removed.push_back(event.GuidValue.GetGuidString());}
};
class Editor final:public GameSproutEditor::GameSproutViewApi{
public:
    explicit Editor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    IProjectAssetsUI* Browser=nullptr;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name)const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name)const override{return name=="ProjectAssetsUI"?Browser:nullptr;}
};
static void Pump(){for(int index=0;index<5;++index)QCoreApplication::processEvents();}
int main(int argc,char** argv){
    QApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    try{
        Check(argc==2,"Pass Runtime directory");runtime=Export(argv[1]);Check(runtime,"Cannot load Runtime");
        QTemporaryDir temporary;Check(temporary.isValid(),"No isolated project");const QString root=temporary.path()+"/Assets";QDir().mkpath(root);
        const QString token="GameSproutDeleteTest-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        Pipeline pipeline;Library library;AssetsManager assets;Scenes scenes;Stage stage;TestPlay play;Host host;Selection selection;
        library.Root=temporary.path().toStdString()+"/Library";
        assets._AssetsRoot=root.toStdString();assets._CanonicalAssetsRoot=QFileInfo(root).canonicalFilePath().toStdString();assets._SourceDatabaseReady=true;assets._Pipeline=&pipeline;assets._Library=&library;
        auto add=[&](const std::string& guid,const std::string& path,eAssetImporterType type=eAssetImporterType::Mesh){
            assets._Assets.emplace(guid,new Meta(guid,path,type));assets._AssetGuidsByPath[path]=guid;
            AssetSourceRecord record;record.GuidString=guid;record.Path=path;record.Importer=type;Json::Value metadata;metadata["Version"]=1;metadata["Guid"]=guid;metadata["Path"]=path;metadata["ImporterType"]=AssetMetaFormat::ImporterTypeName(type);metadata["ImportSettings"]=Json::Value(Json::objectValue);metadata["SubAssets"]=Json::Value(Json::arrayValue);record.MetaJson=AssetArtifactIO::JsonText(metadata);
            assets._SourceRecords[guid]=record;assets._SourceGuidsByPath[path].insert(guid);
            library.Rows["AssetSources/"+guid]={1};
            if(type==eAssetImporterType::Folder)QDir().mkpath(root+'/'+QString::fromStdString(path));
            else Write(root+'/'+QString::fromStdString(path),"generated fixture");
            Write(root+'/'+QString::fromStdString(path)+".meta",QByteArray::fromStdString(record.MetaJson));
        };
        const std::string meshGuid="11111111111111111111111111111111",folderGuid="22222222222222222222222222222222",childGuid="33333333333333333333333333333333",otherGuid="44444444444444444444444444444444";
        const auto meshPath=(token+".mesh").toStdString();add(meshGuid,meshPath);
        add(folderGuid,(token+"-Folder").toStdString(),eAssetImporterType::Folder);add(childGuid,(token+"-Folder/Child.mesh").toStdString());add(otherGuid,"Protected.scene",eAssetImporterType::Scene);
        scenes.Scene=runtime->CreateScene("Reference owner");auto* object=scenes.Scene->CreateGameObject("Keep this object");
        auto* mesh=object->AddComponent<GameSproutRuntime::IMeshComponent>();mesh->SetMesh({meshGuid,17});
        const auto objectID=object->GetInstanceID();
        Editor editor(runtime);editor.Modules={{"AssetsManager",&assets},{"SceneManager",&scenes},{"PrefabStage",&stage},{"PlayMode",&play},{"RuntimeHost",&host},{"SelectionManager",&selection}};
        AssetOperations operations;editor.Modules["AssetOperations"]=&operations;GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        operations.BeforeInitialize(&api);Check(operations.Initialize(&api)&&operations.Launch(&api),"Operations init");operations.LaunchFinished(&api);
        AssetDeletePlan plan;std::string error;
        Check(operations.PrepareDelete({childGuid,folderGuid,folderGuid},plan,error)&&plan.Sources.size()==1&&plan.Sources.front().Guid==folderGuid,"Selected parent did not subsume child");
        scenes.Path="Protected.scene";Check(!operations.PrepareDelete({otherGuid},plan,error),"Open scene source can be deleted");
        auto* protectedSource=static_cast<Meta*>(assets.QueryAsset(Guid(otherGuid)));
        protectedSource->Path="Renamed.scene";
        operations.OnAssetModify(AssetEvent(eAssetMetaEvent::Modify,Guid(otherGuid),"Renamed.scene","Protected.scene",eAssetImporterType::Scene,eAssetImportState::Ready,{}));
        Check(!operations.PrepareDelete({otherGuid},plan,error),"A renamed open scene escaped its stable GUID protection");
        protectedSource->Path=(token+"-Folder/Renamed.scene").toStdString();
        Check(!operations.PrepareDelete({folderGuid},plan,error),"An ancestor of a moved open scene escaped protection");
        protectedSource->Path="Protected.scene";
        scenes.Path.clear();scenes.MainPath="Protected.scene";Check(!operations.PrepareDelete({otherGuid},plan,error),"Suspended main source can be deleted");scenes.MainPath.clear();
        stage.State.Active=true;stage.State.Path=meshPath;stage.State.Guid=meshGuid;Check(!operations.PrepareDelete({meshGuid},plan,error),"Open Prefab source can be deleted");
        auto* stageSource=static_cast<Meta*>(assets.QueryAsset(Guid(meshGuid)));stageSource->Path=(token+"-Folder/Renamed.prefab").toStdString();
        Check(!operations.PrepareDelete({folderGuid},plan,error),"An ancestor of a renamed Prefab source escaped GUID protection");stageSource->Path=meshPath;stage.State={};
        play.State.State=EditorPlayState::Playing;Check(!operations.PrepareDelete({meshGuid},plan,error),"Play allows asset deletion");play.State.State=EditorPlayState::Edit;
        host.Frame=true;Check(!operations.PrepareDelete({meshGuid},plan,error),"Host frame allows deletion");host.Frame=false;
        Check(operations.PrepareDelete({meshGuid},plan,error),error);
        auto stale=plan;stale.Sources.front().Path+="moved";Check(!operations.Delete(stale,error)&&QFileInfo::exists(root+'/'+QString::fromStdString(meshPath)),"Stale confirmation deleted changed source");
        Check(!assets.DeleteSourceAsset(Guid(meshGuid),"../escape",error),"Unsafe path accepted");
        const auto originalMeta=assets._SourceRecords.at(meshGuid).MetaJson;
        Json::Value replacedMeta;Check(AssetArtifactIO::Parse(originalMeta,replacedMeta),"Fixture Meta parse");replacedMeta["Guid"]="99999999999999999999999999999999";
        Write(root+'/'+QString::fromStdString(meshPath)+".meta",QByteArray::fromStdString(AssetArtifactIO::JsonText(replacedMeta)));
        Check(!operations.Delete(plan,error)&&QFileInfo::exists(root+'/'+QString::fromStdString(meshPath))&&pipeline.Invalidated.empty(),"A replaced disk identity was deleted through a stale confirmation");
        Write(root+'/'+QString::fromStdString(meshPath)+".meta",QByteArray::fromStdString(originalMeta));
        Events events;assets.RegisterAssetListener(&events);LibraryRuntimeResources resources(&assets,&library);const auto revision=resources.GetRevision();
        selection.Value={{meshGuid,0,false}};
        Check(operations.Delete(plan,error),error);
        assets.DispatchAssetEvents();
        Check(!QFileInfo::exists(root+'/'+QString::fromStdString(meshPath))&&!QFileInfo::exists(root+'/'+QString::fromStdString(meshPath)+".meta"),"Source or Meta remained");
        Check(!assets.QueryAsset(Guid(meshGuid))&&!assets._SourceRecords.contains(meshGuid)&&!library.Rows.contains("AssetSources/"+meshGuid),"Live asset/database record remained");
        Check(assets._BackupSourcesByPath[meshPath].contains(meshGuid)&&std::any_of(library.Rows.begin(),library.Rows.end(),[](const auto& value){return value.first.starts_with("AssetSourceBackups/");}),"Deleted identity has no durable backup");
        Check(std::count(pipeline.Invalidated.begin(),pipeline.Invalidated.end(),meshGuid)==1&&events.Removed==std::vector<std::string>{meshGuid}&&resources.GetRevision()>revision,"Delete did not invalidate jobs and cached Runtime resources once");
        Check(runtime->QueryGameObject(objectID)==object&&mesh->GetMesh()==GameSproutRuntime::ResourceReference{meshGuid,17}&&selection.Value.empty(),"Deleting a source destroyed a scene object or cleared its resource identity");
        GameSproutRuntime::MeshRenderData data;Check(!resources.LoadMesh({meshGuid,17},data,error)&&!error.empty(),"Deleted resource did not resolve Missing");
        const auto removedCount=events.Removed.size();assets.RemoveAssetRecords((root+'/'+QString::fromStdString(meshPath)).toStdString(),false);assets.DispatchAssetEvents();Check(events.Removed.size()==removedCount,"Delayed watcher duplicate emitted another removal");
        Check(operations.PrepareDelete({folderGuid,childGuid},plan,error),error);
        const auto unsafeChild=root+'/'+token+"-Folder/External.link";Check(QFile::link(root+"/Protected.scene",unsafeChild),"Cannot create isolated directory symlink");
        Check(!operations.Delete(plan,error)&&assets.QueryAsset(Guid(childGuid)),"Folder traversal followed an external link");QFile::remove(unsafeChild);
        Check(operations.Delete(plan,error),error);assets.DispatchAssetEvents();
        Check(!assets.QueryAsset(Guid(folderGuid))&&!assets.QueryAsset(Guid(childGuid))&&assets._BackupSourcesByPath[(token+"-Folder/Child.mesh").toStdString()].contains(childGuid),"Folder deletion did not archive descendants");
        const auto linkPath=root+"/Link.mesh";QFile::link(root+"/Protected.scene",linkPath);add("55555555555555555555555555555555","Placeholder.mesh");
        auto* link=static_cast<Meta*>(assets._Assets.at("55555555555555555555555555555555"));assets._AssetGuidsByPath.erase(link->Path);link->Path="Link.mesh";assets._AssetGuidsByPath[link->Path]="55555555555555555555555555555555";
        Check(!assets.DeleteSourceAsset(link->_Guid,link->Path,error)&&QFileInfo::exists(root+"/Protected.scene"),"A symlink deletion affected its target");
        // Real browser shortcut routing must not intercept Delete inside search.
        editor.Browser=CreateDeletionBrowser(&assets);Check(editor.Browser,"Browser init");
        auto* view=CreateDeletionView(&editor);Check(view,"Deletion View registration");
        auto* widget=DeletionBrowserWidget(editor.Browser);widget->show();Pump();Check(SelectDeletionSource(editor.Browser,otherGuid),"Select protected fixture");
        auto* search=widget->findChild<QLineEdit*>("searchEdit");search->setText("abc");search->setFocus();search->setCursorPosition(0);QTest::keyClick(search,Qt::Key_Delete);Pump();
        Check(search->text()=="bc"&&!DeletionDialog(view),"Delete in search triggered resource deletion");search->clear();Pump();SelectDeletionSource(editor.Browser,otherGuid);
        auto* tree=widget->findChild<QTreeView*>("assetTree");tree->setFocus();QTest::keyClick(tree,Qt::Key_Delete);Pump();
        auto* dialog=DeletionDialog(view);Check(dialog,"Tree Delete did not open .ui confirmation");
        auto* cancel=dialog->findChild<QPushButton*>("cancelButton");Check(cancel,"Missing .ui cancel");cancel->click();Pump();Check(assets.QueryAsset(Guid(otherGuid)),"Cancel deleted a source");
        delete view;QTest::keyClick(tree,Qt::Key_Delete);Pump();Check(assets.QueryAsset(Guid(otherGuid)),"Released action retained callbacks");delete editor.Browser;editor.Browser=nullptr;
        assets.UnregisterAssetListener(&events);operations.Release(&api);runtime->DestroyScene(scenes.Scene);delete runtime;runtime=nullptr;
        for(auto& [guid,meta]:assets._Assets)delete meta;assets._Assets.clear();
        std::cout<<"PASS source/meta trash, source backup/removal batch, folder descendants, stale IDs, protected documents, unchanged references, Runtime invalidation, tree-only shortcuts and cancellation\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL "<<exception.what()<<'\n';delete runtime;return 1;}
}

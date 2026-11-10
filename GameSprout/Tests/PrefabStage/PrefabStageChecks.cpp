#include "PrefabStageTestSupport.h"

namespace {
const Guid SourceGuid("1234567890abcdef1234567890abcdef");
const Guid InvalidGuid("234567890abcdef1234567890abcdef1");
class Fixture {
public:
    GameSproutRuntime::IGameSproutRuntime* Runtime=nullptr;
    QTemporaryDir Temporary;
    TestProject Project;
    TestAssets Assets;
    TestLibrary Library;
    TestSelection Selection;
    TestEditor* Editor=nullptr;
    GameSproutEditor::GameSproutModuleApi* Api=nullptr;
    std::vector<GameSproutEditor::IGameSproutModule*> Modules;
    ISceneManager* Scenes=nullptr;
    IPrefabManager* Prefabs=nullptr;
    IPrefabStage* Stage=nullptr;
    std::string Path="Prefabs/Thing.prefab";
    uint64_t MainID=0,FirstID=0,SecondID=0,MarkerID=0;

    explicit Fixture(const char* runtimeDirectory) {
        Runtime=Export(runtimeDirectory);Check(Runtime,"Cannot load independent Runtime");
    }
    ~Fixture() {
        if(Api)for(auto iterator=Modules.rbegin();iterator!=Modules.rend();++iterator)
            try{GameSproutEditor::GameSproutEditorFinal::Release(**iterator,*Api);}catch(...){}
        // GetModule exports module-owned static instances; Release detaches them.
        delete Api;delete Editor;delete Runtime;
    }
    QString Full()const{return QString::fromStdString(Project.Root+"/Assets/"+Path);}
    void Initialize() {
        Check(Temporary.isValid(),"Temporary project unavailable");QDir root(Temporary.path());
        Check(root.mkpath("Assets/Scenes")&&root.mkpath("Assets/Prefabs")&&root.mkpath("Library/Artifact"),"Cannot create fixture project");
        Project.Root=Temporary.path().toStdString();Editor=new TestEditor(Runtime);
        Modules={LoadModule(SCENE_MODULE_FILE),LoadModule(PREFAB_MODULE_FILE),LoadModule(STAGE_MODULE_FILE)};
        Scenes=dynamic_cast<ISceneManager*>(Modules[0]);Prefabs=dynamic_cast<IPrefabManager*>(Modules[1]);Stage=dynamic_cast<IPrefabStage*>(Modules[2]);
        Check(Scenes&&Prefabs&&Stage,"Unexpected module export interfaces");
        Editor->Modules={{"Project",&Project},{"AssetsManager",&Assets},{"LibraryManager",&Library},{"SelectionManager",&Selection},
            {"SceneManager",Modules[0]},{"PrefabManager",Modules[1]},{"PrefabStage",Modules[2]}};
        Api=new GameSproutEditor::GameSproutModuleApi(nullptr,Editor);
        for(auto* module:Modules)GameSproutEditor::GameSproutEditorFinal::Before(*module,*Api);
        for(auto* module:Modules)Check(GameSproutEditor::GameSproutEditorFinal::Initialize(*module,*Api),"Production module Initialize failed");
        for(auto* module:Modules)Check(GameSproutEditor::GameSproutEditorFinal::Launch(*module,*Api),"Production module Launch failed");
        for(auto* module:Modules)GameSproutEditor::GameSproutEditorFinal::Finished(*module,*Api);
        auto* main=Scenes->GetActiveScene();MainID=main->GetInstanceID();
        auto* original=main->CreateGameObject("Thing");
        auto* originalChild=main->CreateGameObject("Child",original);
        Check(originalChild->SetStaticFlags(GameSproutRuntime::StaticFlags::ContributeGI),"Cannot set source static declarations");
        std::string error;Check(Prefabs->SavePrefab(original->GetInstanceID(),Path,error),error);Publish();
        const GameSproutRuntime::ResourceReference reference{SourceGuid.GetGuidString(),1};
        Check(Prefabs->InstantiatePrefab(reference,0,FirstID,error)&&Prefabs->InstantiatePrefab(reference,0,SecondID,error),error);
        auto* first=Runtime->QueryGameObject(FirstID);auto* second=Runtime->QueryGameObject(SecondID);
        SetPosition(*Scenes,first->GetChild(0)->GetInstanceID(),{7,0,0});SetPosition(*Scenes,second->GetChild(0)->GetInstanceID(),{9,0,0});
        Check(Scenes->SetObjectStaticFlags(first->GetChild(0)->GetInstanceID(),GameSproutRuntime::StaticFlags::NavigationStatic,
            GameSproutRuntime::StaticFlags::Everything,false,error),error);
        SetScale(*Scenes,FirstID,{4,4,4});
        auto* local=main->CreateGameObject("Instance-only child",first);Check(local,"Cannot add instance-only child");
        auto* marker=main->CreateGameObject("Main marker");MarkerID=marker->GetInstanceID();
        Check(Scenes->SaveSceneAs("Scenes/Main.scene",error),error);
        Check(Scenes->EditObject(MarkerID,"Unsaved main name",[](GameSproutRuntime::IGameObject& object,std::string&){object.SetName("Unsaved main marker");return true;},
            [](GameSproutRuntime::IGameObject& object,std::string&){object.SetName("Main marker");return true;},error),error);
        Selection.SetSceneObjectSelection({MainID,FirstID});Pump();
    }
    void Publish() {
        if(!Assets.Meta)Assets.Meta=new TestMeta(SourceGuid,Path);else Assets.Meta->Path=Path;
        if(!Library.Asset)Library.Asset=new TestAsset(SourceGuid);
        const auto bytes=Read(Full());Json::Value data;std::string error;Check(PrefabDefinition::Validate(Parse(bytes),data,error),error);
        std::vector<uint8_t> encoded;Check(PrefabDocument::Encode(data,encoded,error),error);
        Library.Root=Project.Root+"/Library";Library.Asset->Path=Library.Root+"/Artifact/"+SourceGuid.GetGuidString()+"_1.gsasset";Library.Asset->Size=encoded.size();
        Write(QString::fromStdString(Library.Asset->Path),QByteArray(reinterpret_cast<const char*>(encoded.data()),static_cast<qsizetype>(encoded.size())));
        Library.Manifest.GuidString=SourceGuid.GetGuidString();Library.Manifest.SourcePath=Path;Library.Manifest.SourceHash=AssetArtifactIO::Hash(bytes);Library.Manifest.Assets.clear();
        AssetArtifact artifact;artifact.Type=eAssetType::Prefab;artifact.SourceKey="main";artifact.LocalID=1;Library.Manifest.Assets.push_back(artifact);
        if(!QFileInfo::exists(Full()+".meta")){Json::Value meta;meta["Guid"]=SourceGuid.GetGuidString();Write(Full()+".meta",QByteArray::fromStdString(Text(meta)));}
    }
    void AssetEvent(eAssetMetaEvent action,const std::string& oldPath={}) {
        const ::AssetEvent event(action,SourceGuid,Path,oldPath,eAssetImporterType::Prefab,eAssetImportState::Ready,{});
        const auto listeners=Assets.Listeners;for(auto* listener:listeners) {
            if(action==eAssetMetaEvent::Remove)listener->OnAssetRemove(event);else listener->OnAssetModify(event);
        }
        Pump();
    }
    GameSproutRuntime::IGameObject* Content(){return Runtime->QueryGameObject(Stage->GetState().ContentRootObjectID);}
};

#include "PrefabStageScriptChecks.h"
#include "LargeDocumentChecks.h"

void CheckOpenDiscardAndHistory(Fixture& fixture) {
    std::string error;auto* main=fixture.Scenes->GetActiveScene();auto* first=fixture.Runtime->QueryGameObject(fixture.FirstID);
    auto* marker=fixture.Runtime->QueryGameObject(fixture.MarkerID);const auto sourceBytes=Read(fixture.Full());
    Check(fixture.Scenes->HasUnsavedChanges()&&fixture.Scenes->CanUndo(),"Main fixture must contain unsaved history");
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),"Open Prefab Stage failed: "+error);Pump();
    const auto state=fixture.Stage->GetState();auto* root=fixture.Content();
    Check(root->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::ContributeGI,"Stage did not restore source StaticFlags");
    Check(state.Active&&!state.Dirty&&root&&state.SceneID!=fixture.MainID&&fixture.Scenes->GetActiveScene()->GetInstanceID()==state.SceneID,
        "Stage did not create a clean isolated editing scene");
    Check(fixture.Scenes->GetMainScene()==main&&fixture.Runtime->QueryGameObject(fixture.FirstID)==first&&fixture.Runtime->QueryGameObject(fixture.MarkerID)==marker,
        "Opening Stage destroyed or rebuilt the MainStage world");
    Check(!main->IsSimulationEnabled()&&main->IsActiveInHierarchy()&&fixture.Scenes->GetActiveScene()->IsSimulationEnabled(),
        "Opening Stage failed to isolate main simulation while keeping its hierarchy renderable");
    Check(fixture.Selection.GetSceneObjectSelection()==SceneObjectSelection{state.SceneID,root->GetInstanceID()},"Opening Stage did not select its content root");
    auto* child=root->GetChild(0);const auto childID=child->GetInstanceID();auto* transform=child->GetTransform();
    const SceneTransformState preview{{2,3,4},transform->GetLocalRotation(),transform->GetLocalScale()};
    Check(fixture.Scenes->BeginTransformEdit(childID,"Stage drag",error)&&fixture.Scenes->PreviewTransformEdit(preview,error),error);
    Check(!fixture.Stage->GetState().Dirty&&!fixture.Stage->Save(error)&&Read(fixture.Full())==sourceBytes,
        "Uncommitted stage drag published dirty state or changed the prefab source");
    Check(fixture.Scenes->CancelTransformEdit(error)&&!fixture.Stage->GetState().Dirty&&fixture.Stage->GetState().Error.empty(),
        "Cancelled drag dirtied the stage or failed to clear preview state");
    Check(fixture.Scenes->BeginTransformEdit(childID,"Stage drag",error)&&fixture.Scenes->PreviewTransformEdit(preview,error)&&
        fixture.Scenes->CommitTransformEdit(error),error);
    Check(fixture.Stage->GetState().Dirty&&fixture.Stage->GetState().Error.empty(),"Committed drag did not mark the stage dirty");
    Check(fixture.Scenes->Undo(error)&&root->GetChild(0)->GetTransform()->GetLocalPosition().X==0&&!fixture.Stage->GetState().Dirty,
        "Undo did not restore the stage's saved baseline");
    Check(fixture.Scenes->Redo(error)&&root->GetChild(0)->GetTransform()->GetLocalPosition().X==2,error);
    Check(!fixture.Stage->Close(false,error)&&fixture.Stage->GetState().Active,"Dirty Stage closed without an explicit discard/save");
    Check(fixture.Stage->Close(true,error),error);Pump();
    Check(!fixture.Stage->GetState().Active&&fixture.Scenes->GetActiveScene()==main&&Read(fixture.Full())==sourceBytes,
        "Discard failed to restore original main scene or wrote the Prefab source");
    Check(main->IsSimulationEnabled(),"Closing Stage failed to restore main simulation");
    Check(fixture.Selection.GetSceneObjectSelection()==SceneObjectSelection{fixture.MainID,fixture.FirstID},"Closing Stage lost the main scene selection");
    Check(fixture.Scenes->HasUnsavedChanges()&&marker->GetName()=="Unsaved main marker","Stage isolation discarded unsaved main edits");
    Check(fixture.Scenes->Undo(error)&&marker->GetName()=="Main marker", "Stage Undo history polluted the main scene: "+error);
    Check(fixture.Scenes->Redo(error)&&marker->GetName()=="Unsaved main marker",error);
}

void CheckSavingAndPropagation(Fixture& fixture) {
    std::string error;const auto before=Parse(Read(fixture.Full()));const auto metaBefore=Read(fixture.Full()+".meta");
    auto* main=fixture.Scenes->GetActiveScene();auto* first=fixture.Runtime->QueryGameObject(fixture.FirstID);auto* second=fixture.Runtime->QueryGameObject(fixture.SecondID);
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),error);auto* root=fixture.Content();
    const auto stageSceneID=fixture.Stage->GetState().SceneID;SetScale(*fixture.Scenes,root->GetInstanceID(),{2,3,2});
    SetScale(*fixture.Scenes,root->GetChild(0)->GetInstanceID(),{1,5,1});
    Check(fixture.Scenes->SetObjectStaticFlags(root->GetChild(0)->GetInstanceID(),GameSproutRuntime::StaticFlags::OccluderStatic,
        GameSproutRuntime::StaticFlags::Everything,false,error),error);
    Check(fixture.Stage->Save(error),"Stage save failed: "+error);Pump();
    Check(fixture.Stage->GetState().Active&&!fixture.Stage->GetState().Dirty&&fixture.Stage->GetState().SceneID==stageSceneID,
        "Saving closed/rebuilt Stage or left it dirty");
    const auto after=Parse(Read(fixture.Full()));
    Check(after["Data"]["Objects"][1]["StaticFlags"].asUInt()==2,"Stage save omitted static declarations from source data");
    Check(Read(fixture.Full()+".meta")==metaBefore,"Stage save changed the source GUID/Meta");
    for(Json::ArrayIndex index=0;index<before["Data"]["Objects"].size();++index) {
        Check(before["Data"]["Objects"][index]["ID"]==after["Data"]["Objects"][index]["ID"],"Stage save changed an existing source node identity");
        const auto& components=before["Data"]["Objects"][index]["Components"];
        for(Json::ArrayIndex component=0;component<components.size();++component)
            Check(components[component]["ID"]==after["Data"]["Objects"][index]["Components"][component]["ID"],"Stage save changed an existing source component identity");
    }
    Check(fixture.Scenes->GetMainScene()==main&&fixture.Runtime->QueryGameObject(fixture.FirstID)==first&&fixture.Runtime->QueryGameObject(fixture.SecondID)==second,
        "Stage save rebuilt the suspended main scene/instances");
    Check(!main->IsSimulationEnabled()&&main->IsActiveInHierarchy(),"Background source propagation resumed or hid the main simulation");
    Check(first->GetTransform()->GetLocalScale().X==4&&second->GetTransform()->GetLocalScale().X==2,
        "Saving source failed to inherit unmodified values while preserving per-instance root overrides");
    Check(first->GetChild(0)->GetTransform()->GetLocalPosition().X==7&&second->GetChild(0)->GetTransform()->GetLocalPosition().X==9&&
          first->GetChild(0)->GetTransform()->GetLocalScale().Y==5&&second->GetChild(0)->GetTransform()->GetLocalScale().Y==5&&first->FindChild("Instance-only child"),
        "Saving source lost independent property/structural overrides or failed to propagate uncovered values");
    Check(first->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::NavigationStatic&&
        second->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::OccluderStatic,
        "Stage save did not propagate StaticFlags while preserving each instance override");
    Check(!fixture.Scenes->BeginPlaySession(error),"Stage document was allowed to become the gameplay scene");
    Check(fixture.Stage->Close(false,error)&&fixture.Scenes->GetActiveScene()==main&&fixture.Scenes->HasUnsavedChanges(),error);
}

void CheckFailureBoundaries(Fixture& fixture) {
    std::string error;auto* main=fixture.Scenes->GetActiveScene();const auto mainID=main->GetInstanceID();
    fixture.Assets.Additional.emplace(InvalidGuid.GetGuidString(),new TestMeta(InvalidGuid,"Prefabs/Broken.prefab"));
    Write(QString::fromStdString(fixture.Project.Root+"/Assets/Prefabs/Broken.prefab"),"not a prefab");
    Check(!fixture.Stage->OpenPrefab(InvalidGuid.GetGuidString(),error)&&fixture.Scenes->GetActiveScene()==main&&!fixture.Stage->GetState().Active,
        "Invalid source switched or destroyed the current main scene");
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),error);auto* root=fixture.Content();
    const auto stageID=fixture.Stage->GetState().SceneID;SetScale(*fixture.Scenes,root->GetInstanceID(),{8,8,8});
    Check(!fixture.Stage->OpenPrefab(InvalidGuid.GetGuidString(),error)&&fixture.Stage->GetState().SceneID==stageID&&fixture.Content()==root,
        "Opening another source discarded the dirty Stage");
    const auto source=Read(fixture.Full());Write(fixture.Full(),source+"\n");const auto externallyChanged=Read(fixture.Full());
    Check(!fixture.Stage->Save(error)&&Read(fixture.Full())==externallyChanged&&fixture.Stage->GetState().Dirty,
        "External source hash conflict was overwritten or dirtiness cleared");
    Check(fixture.Stage->Close(true,error),error);Write(fixture.Full(),source);fixture.Publish();
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),error);root=fixture.Content();
    Check(!fixture.Scenes->DeleteObject(root->GetInstanceID(),error),"Protected Prefab content root was deleted through authoring API");
    auto* stage=fixture.Scenes->GetActiveScene();auto* stray=stage->CreateGameObject("Out-of-content sibling");
    Check(stray,"Cannot construct malformed preview scene");const auto untouched=Read(fixture.Full());
    Check(!fixture.Stage->Save(error)&&Read(fixture.Full())==untouched,"Stage silently omitted a sibling outside its content root");
    Check(fixture.Stage->Close(true,error),error);
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),error);stage=fixture.Scenes->GetActiveScene();root=fixture.Content();
    Check(stage->DestroyGameObject(root),"Cannot construct deleted content root preview");
    Check(!fixture.Stage->Save(error)&&Read(fixture.Full())==untouched,"Stage saved a deleted/missing content root");
    Check(fixture.Stage->Close(true,error)&&fixture.Scenes->GetActiveScene()->GetInstanceID()==mainID,error);
}

void CheckSourceEventsAndPlay(Fixture& fixture) {
    std::string error;auto* main=fixture.Scenes->GetActiveScene();main->SetSimulationEnabled(false);
    Check(fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error),error);
    const auto oldPath=fixture.Path;const auto oldFull=fixture.Full();fixture.Path="Prefabs/Moved.prefab";
    Check(QFile::rename(oldFull,fixture.Full())&&QFile::rename(oldFull+".meta",fixture.Full()+".meta"),"Cannot move source fixture");
    fixture.Assets.Meta->Path=fixture.Path;fixture.AssetEvent(eAssetMetaEvent::Modify,oldPath);
    Check(fixture.Stage->GetState().Path==fixture.Path&&!fixture.Stage->GetState().MissingSource,"Stage did not track source relocation by GUID");
    const auto source=Read(fixture.Full());
    Check(QFile::remove(fixture.Full()),"Cannot remove source fixture");fixture.AssetEvent(eAssetMetaEvent::Remove);
    Check(fixture.Stage->GetState().MissingSource&&!fixture.Stage->Save(error),"Missing source was silently recreated or ignored");
    Check(fixture.Stage->Close(true,error),error);
    Check(!main->IsSimulationEnabled()&&main->IsActiveInHierarchy(),"Closing Stage failed to preserve the main scene's preexisting simulation setting");
    main->SetSimulationEnabled(true);
    Write(fixture.Full(),source);fixture.Publish();
    Check(fixture.Scenes->BeginPlaySession(error),"Cannot begin isolated main Play fixture: "+error);
    const auto playingScene=fixture.Scenes->GetActiveScene()->GetInstanceID();
    Check(!fixture.Stage->OpenPrefab(SourceGuid.GetGuidString(),error)&&fixture.Scenes->GetActiveScene()->GetInstanceID()==playingScene,
        "Prefab Stage was opened while gameplay scene was active");
    Check(fixture.Scenes->EndPlaySession(error),error);
}
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        Check(argc==2||argc==3,"Pass the independently built Runtime directory and optional read-only user Assets directory");
        if(argc==3&&std::string(argv[2])=="--large-documents") {
            CheckLargeSceneDocument(argv[1]);
            CheckLargeSourceAndTemporaryDocuments(argv[1]);
            CheckLargePreparedPrefab(argv[1]);
            return 0;
        }
        {
            Fixture fixture(argv[1]);fixture.Initialize();
            CheckOpenDiscardAndHistory(fixture);CheckSavingAndPropagation(fixture);CheckFailureBoundaries(fixture);CheckSourceEventsAndPlay(fixture);
        }
        {
            Fixture fixture(argv[1]);fixture.Initialize();CheckScriptFieldEvolution(fixture);
        }
        if(argc==3)for(const auto* filename:{"Mesh.prefab","Mesh 1.prefab","Mesh 2.prefab"}) {
            Fixture fixture(argv[1]);fixture.Initialize();CheckUserPrefabSchema(fixture,QDir(QString::fromUtf8(argv[2])).filePath(QString::fromUtf8(filename)));
        }
        std::cout<<"Prefab Stage independent document, save propagation, override preservation and failure boundaries passed\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}

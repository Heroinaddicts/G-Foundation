#pragma once
#include "../../Module/SelectionManager/SelectionManager.h"

class PlaySelectionAssets final : public IAssetsManager {
public:
    IAssetMeta* QueryAsset(const Guid&) const override {return nullptr;}
    bool ReimportAsset(const Guid&) override {return false;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override {return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override {return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override {return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override {return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override {return {};}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override {return {};}
    bool RegisterAssetListener(IAssetListener* listener) override {Listener=listener;return listener!=nullptr;}
    void UnregisterAssetListener(IAssetListener* listener) override {if(Listener==listener)Listener=nullptr;}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override {return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override {}
    IAssetListener* Listener=nullptr;
};
class PlaySelectionRegistry final : public GameSproutEditor::GameSproutViewApi {
public:
    explicit PlaySelectionRegistry(GameSproutEditor::GameSproutViewApi& source)
        :GameSproutViewApi(source._Runtime),Source(source) {}
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override {
        if(name=="AssetsManager")return const_cast<PlaySelectionAssets*>(&Assets);
        return Source.FindGameSproutModule(name);
    }
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string& name) const override {return Source.FindGameSproutView(name);}
    GameSproutEditor::GameSproutViewApi& Source;
    PlaySelectionAssets Assets;
};
class PlaySelectionScope {
public:
    PlaySelectionScope(SelectionManager& selection,GameSproutEditor::GameSproutModuleApi& api):Selection(selection),Api(api) {}
    ~PlaySelectionScope(){GameSproutEditor::GameSproutEditorFinal::Release(Selection,Api);}
    SelectionManager& Selection;
    GameSproutEditor::GameSproutModuleApi& Api;
};

static void CheckPlaySceneSelection(SceneManager& manager,GameSproutEditor::GameSproutModuleApi& source,
                                    GameSproutRuntime::IGameSproutRuntime* runtime) {
    PlaySelectionRegistry registry(*source._EditorApi);
    GameSproutEditor::GameSproutModuleApi api(source._Foundation,&registry);
    SelectionManager selection;PlaySelectionScope scope(selection,api);
    Check(GameSproutEditor::GameSproutEditorFinal::Initialize(selection,api)&&
          GameSproutEditor::GameSproutEditorFinal::Launch(selection,api),"Cannot launch production SelectionManager");
    auto* scene=manager.GetActiveScene();auto* author=scene->CreateGameObject("Selected unsaved author");
    const auto authorFileID=manager.GetObjectFileID(author->GetInstanceID());
    const auto originalID=author->GetInstanceID();
    selection.SetSceneObjectSelection({scene->GetInstanceID(),originalID});
    std::string error;
    Check(manager.BeginPlaySession(error),error);
    auto* mapped=manager.QueryObjectByFileID(authorFileID);
    Check(mapped&&mapped->GetInstanceID()!=originalID&&selection.GetSceneObjectSelection()==
          SceneObjectSelection{manager.GetActiveScene()->GetInstanceID(),mapped->GetInstanceID()},"Play did not map selection using FileID");
    const auto playID=mapped->GetInstanceID();
    Check(manager.GetActiveScene()->DestroyGameObject(mapped),"Cannot destroy selected Play node");
    runtime->Update(0);
    Check(selection.GetSceneObjectSelection().ObjectID==0,"Play deletion retained stale object selection");
    auto* runtimeOnly=manager.GetActiveScene()->CreateGameObject("Temporary selection");
    selection.SetSceneObjectSelection({manager.GetActiveScene()->GetInstanceID(),runtimeOnly->GetInstanceID()});
    Check(manager.EndPlaySession(error),error);
    mapped=manager.QueryObjectByFileID(authorFileID);
    Check(mapped&&mapped->GetInstanceID()!=playID&&mapped->GetInstanceID()==originalID&&selection.GetSceneObjectSelection()==
          SceneObjectSelection{manager.GetActiveScene()->GetInstanceID(),mapped->GetInstanceID()},
          "Stop did not restore original author selection after Play deletion/temporary selection");
    runtime->Update(0);GameSproutEditor::GameSproutEditorFinal::Update(selection,api);
    Check(selection.GetSceneObjectSelection().ObjectID==mapped->GetInstanceID(),"Late destroy events cleared restored selection");
    // FileID 0 is the scene root, distinct from a missing ordinary node.
    selection.SetSceneObjectSelection({manager.GetActiveScene()->GetInstanceID(),manager.GetActiveScene()->GetInstanceID()});
    Check(manager.BeginPlaySession(error),error);
    Check(selection.GetSceneObjectSelection().ObjectID==manager.GetActiveScene()->GetInstanceID(),"Play lost scene root selection");
    selection.SetSceneObjectSelection({});
    Check(manager.EndPlaySession(error)&&selection.GetSceneObjectSelection().ObjectID==manager.GetActiveScene()->GetInstanceID(),
          "Stop failed to restore root selection");
    const std::vector<ProjectAssetSelection> authorAssets={{"11111111111111111111111111111111",0,false}};
    selection.SetAssetSelection(authorAssets);
    Check(manager.BeginPlaySession(error),error);
    Check(selection.GetAssetSelection()==authorAssets&&!selection.GetSceneObjectSelection().ObjectID,"Play changed asset selection");
    selection.SetAssetSelection({{"22222222222222222222222222222222",42,true}});
    Check(manager.EndPlaySession(error)&&selection.GetAssetSelection()==authorAssets&&!selection.GetSceneObjectSelection().ObjectID,
          "Temporary Play asset selection replaced author selection");
    Check(manager.BeginPlaySession(error),error);
    const AssetEvent removed(eAssetMetaEvent::Remove,Guid(authorAssets.front().GuidString),"Deleted.png","",
                             eAssetImporterType::Texture,eAssetImportState::NotRequired,"");
    selection.OnAssetRemove(removed);
    Check(manager.EndPlaySession(error)&&selection.GetAssetSelection().empty(),"Stop restored a source asset deleted during Play");
    selection.SetAssetSelection(authorAssets);
    // Failed preparation emits no session events and leaves the current selection unchanged.
    manager.GetActiveScene()->SetName("");
    Check(!manager.BeginPlaySession(error)&&selection.GetAssetSelection()==authorAssets,"Failed Play preparation altered selection");
    manager.GetActiveScene()->SetName("PlaySession");
}

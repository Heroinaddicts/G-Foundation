#include "ISceneManager.h"
#include "IPrefabManager.h"
#include "IProject.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "GameSproutRuntimeExport.h"
#include "PrefabDocument.h"
#include "../../Module/Shared/PrefabDefinition.h"
#include "../../Module/Shared/AssetArtifactIO.h"
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
    static bool Launch(IGameSproutModule& module,GameSproutModuleApi& api){return module.Launch(&api);}
    static void Finished(IGameSproutModule& module,GameSproutModuleApi& api){module.LaunchFinished(&api);}
    static void Release(IGameSproutModule& module,GameSproutModuleApi& api){module.Release(&api);}
};
}
static void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
static QByteArray Read(const QString& path){QFile file(path);Check(file.open(QIODevice::ReadOnly),"Cannot read "+path.toStdString());return file.readAll();}
static void Write(const QString& path,const QByteArray& bytes){QFile file(path);Check(file.open(QIODevice::WriteOnly|QIODevice::Truncate),"Cannot write "+path.toStdString());Check(file.write(bytes)==bytes.size(),"Short write");}
static Json::Value Parse(const QByteArray& bytes){Json::Value value;Check(AssetArtifactIO::Parse(bytes.toStdString(),value),"Invalid test JSON");return value;}
static std::string Text(const Json::Value& value){return AssetArtifactIO::JsonText(value);}
class TestProject final:public IProject {
public:
    std::string Root;
    const std::string& GetProjectRoot() const override{return Root;}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class TestMeta final:public IAssetMeta {
public:
    TestMeta(const Guid& guid,std::string path):IAssetMeta(eAssetImporterType::Prefab,guid),Path(std::move(path)){}
    std::string Path;
    const std::string& GetPath() const override{return Path;}
    IAssetImportSettings* GetImportSettings() const override{return nullptr;}
    std::vector<IAsset*> GetSubAssets() const override{return {};}
    eAssetImportState GetImportState() const override{return eAssetImportState::Ready;}
    std::string GetImportError() const override{return {};}
    bool SerializeMeta(std::string& text) const override{text="{}";return true;}
    bool SetSubAssets(const std::string&) override{return true;}
    bool SaveMeta() override{return true;}
};
class TestAsset final:public IAsset {
public:
    Guid GuidValue;uint64_t LocalID=1;std::string Name="Prefab",Path,Descriptor="{\"Format\":\"BinaryValueLE1\"}";uint64_t Size=0;
    explicit TestAsset(const Guid& guid):GuidValue(guid){}
    const Guid& GetGuid() const override{return GuidValue;}
    uint64_t GetLocalID() const override{return LocalID;}
    eAssetType GetAssetType() const override{return eAssetType::Prefab;}
    const std::string& GetName() const override{return Name;}
    const std::string& GetArtifactPath() const override{return Path;}
    const std::string& GetDataDescriptorJson() const override{return Descriptor;}
    uint32_t GetDataVersion() const override{return 2;}
    uint64_t GetDataSize() const override{return Size;}
};
class TestAssets final:public IAssetsManager {
public:
    TestMeta* Meta=nullptr;std::vector<IAssetListener*> Listeners;std::map<std::string,TestMeta*> Additional;
    ~TestAssets() override{delete Meta;for(const auto& item:Additional)delete item.second;}
    IAssetMeta* QueryAsset(const Guid& guid) const override {
        const auto key=guid.GetGuidString();if(Meta&&Meta->_Guid.GetGuidString()==key)return Meta;
        const auto found=Additional.find(key);return found==Additional.end()?nullptr:found->second;
    }
    bool ReimportAsset(const Guid&) override{return true;}
    bool ApplyImportSettings(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool LoadMaterialSource(const Guid&,std::string&,std::string&) const override{return false;}
    bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&) override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&) override{return false;}
    std::vector<IAssetMeta*> GetAssetMetas() const override{std::vector<IAssetMeta*> result;if(Meta)result.push_back(Meta);for(const auto& item:Additional)result.push_back(item.second);return result;}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&) const override{return GetAssetMetas();}
    bool RegisterAssetListener(IAssetListener* listener) override{Listeners.push_back(listener);return true;}
    void UnregisterAssetListener(IAssetListener* listener) override{Listeners.erase(std::remove(Listeners.begin(),Listeners.end(),listener),Listeners.end());}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class TestLibrary final:public ILibraryManager {
public:
    std::string Root;AssetImportManifest Manifest;TestAsset* Asset=nullptr;
    std::map<std::string,AssetImportManifest> AdditionalManifests;std::map<std::string,TestAsset*> AdditionalAssets;
    ~TestLibrary() override{delete Asset;for(const auto& item:AdditionalAssets)delete item.second;}
    ILDBSession* GetLDBSession() const override{return nullptr;}
    const std::string& GetLibraryRoot() const override{return Root;}
    std::vector<std::string> GetManifestGuids() const override{return {Manifest.GuidString};}
    bool GetManifest(const Guid& guid,AssetImportManifest& output) const override {
        if(guid.GetGuidString()==Manifest.GuidString){output=Manifest;return true;}
        const auto found=AdditionalManifests.find(guid.GetGuidString());if(found==AdditionalManifests.end())return false;output=found->second;return true;
    }
    bool CommitImport(const std::string&,AssetImportManifest&,std::string&) override{return false;}
    bool RemoveAsset(const Guid&,std::string&,const std::vector<LDBWriteOperation>&) override{return false;}
    std::vector<std::string> FindDependents(const std::string&) const override{return {};}
    IAsset* QueryAsset(const Guid& guid,uint64_t id) override {
        if(Asset&&guid.GetGuidString()==Asset->GuidValue.GetGuidString()&&id==Asset->LocalID)return Asset;
        const auto found=AdditionalAssets.find(guid.GetGuidString());return found!=AdditionalAssets.end()&&id==found->second->LocalID?found->second:nullptr;
    }
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
class TestEditor final:public GameSproutEditor::GameSproutViewApi {
public:
    explicit TestEditor(GameSproutRuntime::IGameSproutRuntime* runtime):GameSproutViewApi(runtime){}
    std::map<std::string,GameSproutEditor::IGameSproutModule*> Modules;
    GameSproutEditor::IGameSproutModule* FindGameSproutModule(const std::string& name) const override{const auto found=Modules.find(name);return found==Modules.end()?nullptr:found->second;}
    GameSproutEditor::IGameSproutView* FindGameSproutView(const std::string&) const override{return nullptr;}
};
class TestModuleScope {
public:
    TestModuleScope(GameSproutEditor::IGameSproutModule& scenes,GameSproutEditor::IGameSproutModule& prefabs,GameSproutEditor::GameSproutModuleApi& api)
        :_Scenes(scenes),_Prefabs(prefabs),_Api(api){}
    ~TestModuleScope(){Stop();}
    void Stop(){if(_Stopped)return;_Stopped=true;GameSproutEditor::GameSproutEditorFinal::Release(_Prefabs,_Api);GameSproutEditor::GameSproutEditorFinal::Release(_Scenes,_Api);}
private:
    GameSproutEditor::IGameSproutModule& _Scenes;GameSproutEditor::IGameSproutModule& _Prefabs;GameSproutEditor::GameSproutModuleApi& _Api;bool _Stopped=false;
};
static GameSproutEditor::IGameSproutModule* LoadModule(const char* path){
    void* library=dlopen(path,RTLD_NOW|RTLD_LOCAL);const char* reason=library?nullptr:dlerror();Check(library,reason?reason:"Cannot load module");
    auto entry=reinterpret_cast<GameSproutEditor::IGameSproutModule*(*)()>(dlsym(library,GameSproutModuleEntryName));Check(entry,"Module export missing");return entry();
}
static void Publish(TestProject& project,TestAssets& assets,TestLibrary& library,const std::string& relativePath){
    const Guid guid("1234567890abcdef1234567890abcdef");
    if(!assets.Meta)assets.Meta=new TestMeta(guid,relativePath);
    if(!library.Asset)library.Asset=new TestAsset(guid);
    const auto bytes=Read(QString::fromStdString(project.Root+"/Assets/"+relativePath));Json::Value data;std::string error;
    Check(PrefabDefinition::Validate(Parse(bytes),data,error),error);
    std::vector<uint8_t> encoded;Check(PrefabDocument::Encode(data,encoded,error),error);
    library.Root=project.Root+"/Library";QDir().mkpath(QString::fromStdString(library.Root+"/Artifact"));
    library.Asset->Path=library.Root+"/Artifact/"+guid.GetGuidString()+"_1.gsasset";library.Asset->Size=encoded.size();
    Write(QString::fromStdString(library.Asset->Path),QByteArray(reinterpret_cast<const char*>(encoded.data()),static_cast<qsizetype>(encoded.size())));
    library.Manifest.GuidString=guid.GetGuidString();library.Manifest.SourcePath=relativePath;library.Manifest.SourceHash=AssetArtifactIO::Hash(bytes);
    const QString meta=QString::fromStdString(project.Root+"/Assets/"+relativePath+".meta");
    if(!QFileInfo::exists(meta)){Json::Value value;value["Guid"]=guid.GetGuidString();Write(meta,QByteArray::fromStdString(Text(value)));}
}
static void SetClassification(ISceneManager& scenes,GameSproutRuntime::IGameObject& object,std::uint8_t layer,std::uint64_t tag) {
    const auto previousLayer=object.GetLayer();const auto previousTag=object.GetTagID();std::string error;
    const auto operation=[](std::uint8_t layer,std::uint64_t tag){return [layer,tag](GameSproutRuntime::IGameObject& node,std::string& reason){
        if(node.SetLayer(layer)&&node.SetTagID(tag))return true;reason="Cannot set authored Tag/Layer";return false;
    };};
    Check(scenes.EditObject(object.GetInstanceID(),"Set classification",operation(layer,tag),operation(previousLayer,previousTag),error),error);
}
static GameSproutRuntime::IScriptComponent* Script(GameSproutRuntime::IGameObject& object){for(std::size_t i=0;i<object.GetComponentCount();++i)if(auto* value=dynamic_cast<GameSproutRuntime::IScriptComponent*>(object.GetComponentAt(i)))return value;return nullptr;}
static Json::Value Data(const TestProject& project,const std::string& path){Json::Value data;std::string error;Check(PrefabDefinition::Validate(Parse(Read(QString::fromStdString(project.Root+"/Assets/"+path))),data,error),error);return data;}
static void SaveData(const TestProject& project,const std::string& path,const Json::Value& data){Write(QString::fromStdString(project.Root+"/Assets/"+path),QByteArray::fromStdString(Text(PrefabDefinition::Source(data))));}
static void SetPosition(ISceneManager& scenes,GameSproutRuntime::IGameObject& object,GameSproutRuntime::Vector3 value){
    std::string error;const auto old=object.GetTransform()->GetLocalPosition();
    Check(scenes.EditObject(object.GetInstanceID(),"Set instance position",[value](GameSproutRuntime::IGameObject& node,std::string&){node.GetTransform()->SetLocalPosition(value);return true;},[old](GameSproutRuntime::IGameObject& node,std::string&){node.GetTransform()->SetLocalPosition(old);return true;},error),error);
}
static void PublishAdditional(TestProject& project,TestAssets& assets,TestLibrary& library,const std::string& path,const Guid& guid) {
    const auto key=guid.GetGuidString();
    if(!assets.Additional.count(key))assets.Additional.emplace(key,new TestMeta(guid,path));
    if(!library.AdditionalAssets.count(key))library.AdditionalAssets.emplace(key,new TestAsset(guid));
    auto* asset=library.AdditionalAssets.at(key);asset->LocalID=7;
    const auto bytes=Read(QString::fromStdString(project.Root+"/Assets/"+path));const auto data=Data(project,path);
    std::vector<uint8_t> encoded;std::string error;Check(PrefabDocument::Encode(data,encoded,error),error);
    library.Root=project.Root+"/Library";QDir().mkpath(QString::fromStdString(library.Root+"/Artifact"));
    asset->Path=library.Root+"/Artifact/"+key+"_7.gsasset";asset->Size=encoded.size();
    Write(QString::fromStdString(asset->Path),QByteArray(reinterpret_cast<const char*>(encoded.data()),static_cast<qsizetype>(encoded.size())));
    AssetImportManifest manifest;manifest.GuidString=key;manifest.SourcePath=path;manifest.SourceHash=AssetArtifactIO::Hash(bytes);
    AssetArtifact main;main.Type=eAssetType::Prefab;main.SourceKey="main";main.LocalID=7;manifest.Assets.push_back(main);
    library.AdditionalManifests[key]=std::move(manifest);
    Json::Value meta;meta["Guid"]=key;Write(QString::fromStdString(project.Root+"/Assets/"+path+".meta"),QByteArray::fromStdString(Text(meta)));
}
static void ReadyEvent(TestAssets& assets,const Guid& guid,const std::string& path) {
    const AssetEvent event(eAssetMetaEvent::Modify,guid,path,{},eAssetImporterType::Prefab,eAssetImportState::Ready,{});
    const auto listeners=assets.Listeners;for(auto* listener:listeners)listener->OnAssetModify(event);
    QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
}
static void CheckReference(ISceneManager& scenes,GameSproutRuntime::IGameSproutRuntime& runtime,GameSproutRuntime::IGameObject& instance){
    auto* script=Script(instance);auto* child=instance.GetChild(0);
    Check(script&&child,"Instance lost script or child");const auto& reference=script->GetFields().at("Test::_Target");
    Check(reference.Kind==GameSproutRuntime::ScriptValueKind::SceneObject&&reference.SceneObject.Assigned&&
          reference.SceneObject.ObjectFileID==scenes.GetObjectFileID(child->GetInstanceID())&&
          runtime.QuerySceneObjectByFileID(scenes.GetActiveScene(),reference.SceneObject.ObjectFileID)==child,
          "Prefab internal script reference escaped its own instance");
}
static const Json::Value& ComponentData(const Json::Value& node,const char* type) {
    for(const auto& component:node["Components"])if(component["Type"]==type)return component;
    throw std::runtime_error(std::string("Missing exported component: ")+type);
}
static void CheckExportReferences(TestProject& project,ISceneManager& scenes,IPrefabManager& prefabs,GameSproutRuntime::IGameSproutRuntime& runtime,
    GameSproutRuntime::IGameObject& source,GameSproutRuntime::IGameObject& child,GameSproutRuntime::IGameObject& external) {
    std::string error;auto* script=Script(source);const auto previous=script->GetFields();
    GameSproutRuntime::ScriptValue internal;internal.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;internal.SceneObject={true,scenes.GetObjectFileID(child.GetInstanceID())};
    auto self=internal;self.SceneObject.ObjectFileID=scenes.GetObjectFileID(source.GetInstanceID());
    auto sceneRoot=internal;sceneRoot.SceneObject.ObjectFileID=0;
    auto empty=internal;empty.SceneObject={false,0};
    GameSproutRuntime::ScriptValue resource;resource.Kind=GameSproutRuntime::ScriptValueKind::Resource;resource.Resource={"4567890123abcdef4567890123abcdef",77};
    GameSproutRuntime::ScriptValue text;text.Kind=GameSproutRuntime::ScriptValueKind::String;text.String="{\"ObjectFileID\":999}";
    Check(script->SetField("Test::_Internal",internal,error)&&script->SetField("Test::_Self",self,error)&&script->SetField("Test::_SceneRoot",sceneRoot,error)&&
          script->SetField("Test::_Empty",empty,error)&&script->SetField("Test::_Resource",resource,error)&&script->SetField("Test::_Text",text,error),error);
    const auto authored=script->GetFields();SceneSubtreeDocument before,after;
    Check(scenes.CaptureObjectDocument(source.GetInstanceID(),before,error),error);
    Check(prefabs.SavePrefab(source.GetInstanceID(),"Prefabs/External.prefab",error),"Export with external references failed: "+error);
    const auto saved=Data(project,"Prefabs/External.prefab");const auto rootID=PrefabDefinition::RootID(saved);
    const auto& fields=ComponentData(saved["Objects"][0],"GameSproutRuntime.ScriptComponent")["Fields"];
    Check(fields["Test::_Target"]["Value"].isNull()&&fields["Test::_SceneRoot"]["Value"].isNull(),"Export retained an external object/assigned scene-root reference");
    Check(fields["Test::_Internal"]["Value"]["ObjectFileID"]==saved["Objects"][1]["ID"]&&fields["Test::_Self"]["Value"]["ObjectFileID"].asUInt64()==rootID&&
          fields["Test::_Empty"]["Value"].isNull(),"Export damaged internal/self/unassigned object references");
    Check(fields["Test::_Resource"]["Value"]["Guid"].asString()==resource.Resource.Guid&&fields["Test::_Resource"]["Value"]["LocalID"].asUInt64()==77&&
          fields["Test::_Text"]["Value"].asString()==text.String,"Export cleared a resource reference or interpreted a string as object data");
    Check(scenes.CaptureObjectDocument(source.GetInstanceID(),after,error)&&before.DocumentJson==after.DocumentJson&&script->GetFields()==authored,
          "Saving the source copy mutated the Runtime object references or its captured document");
    Check(script->ReplaceFields(previous,error),error);

    auto* animationRoot=scenes.GetActiveScene()->CreateGameObject("Animation export");
    auto* animatedChild=scenes.GetActiveScene()->CreateGameObject("Animated child",animationRoot);
    auto* externalAnimator=external.AddComponent<GameSproutRuntime::IAnimatorComponent>();Check(externalAnimator,"Cannot create external animation fixture");
    auto* animator=animationRoot->AddComponent<GameSproutRuntime::IAnimatorComponent>();
    const GameSproutRuntime::ResourceReference skeleton{"5678901234abcdef5678901234abcdef",88};animator->SetSkeleton(skeleton);
    const std::vector<GameSproutRuntime::AnimationNodeBinding> bindings{{"Internal",animatedChild->GetInstanceID()},{"External",external.GetInstanceID()},{"Unbound",0}};
    Check(animator->SetNodeBindings(bindings),"Cannot configure animation export bindings");
    auto* externalSkin=animationRoot->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
    auto* internalSkin=animatedChild->AddComponent<GameSproutRuntime::ISkinnedMeshRendererComponent>();
    Check(externalSkin&&internalSkin&&externalSkin->SetAnimatorObjectID(external.GetInstanceID())&&internalSkin->SetAnimatorObjectID(animationRoot->GetInstanceID()),"Cannot configure skin export bindings");
    Check(prefabs.SavePrefab(animationRoot->GetInstanceID(),"Prefabs/AnimationExternal.prefab",error),"Animation reference export failed: "+error);
    const auto animation=Data(project,"Prefabs/AnimationExternal.prefab");const auto& exportedAnimator=ComponentData(animation["Objects"][0],"GameSproutRuntime.Animator");
    Check(exportedAnimator["Bindings"][0]["ObjectFileID"]==animation["Objects"][1]["ID"]&&exportedAnimator["Bindings"][1]["ObjectFileID"].asUInt64()==0&&
          exportedAnimator["Bindings"][2]["ObjectFileID"].asUInt64()==0&&exportedAnimator["Skeleton"]["Guid"].asString()==skeleton.Guid,
          "Animation export did not preserve internal/resource identities while clearing external bindings");
    Check(ComponentData(animation["Objects"][0],"GameSproutRuntime.SkinnedMeshRenderer")["AnimatorObjectFileID"].asUInt64()==0&&
          ComponentData(animation["Objects"][1],"GameSproutRuntime.SkinnedMeshRenderer")["AnimatorObjectFileID"]==animation["Objects"][0]["ID"],"Skin export did not distinguish internal and external Animator references");
    Check(animator->GetNodeBindings()[1].ObjectID==external.GetInstanceID()&&externalSkin->GetAnimatorObjectID()==external.GetInstanceID()&&
          internalSkin->GetAnimatorObjectID()==animationRoot->GetInstanceID()&&runtime.QueryGameObject(animatedChild->GetInstanceID())==animatedChild,
          "Animation source export changed the live scene bindings");
}
static void CheckExternalCreateAndConnect(TestProject& project,TestAssets& assets,TestLibrary& library,ISceneManager& scenes,IPrefabManager& prefabs,GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* scene=scenes.GetActiveScene();auto* root=scene->CreateGameObject("External reference creator");auto* child=scene->CreateGameObject("Internal child",root);
    auto* outside=scene->CreateGameObject("Creation external target");const auto rootID=root->GetInstanceID(),childID=child->GetInstanceID();std::string error;
    const GameSproutRuntime::ResourceReference scriptSource{"3456789012abcdef3456789012abcdef",1};Check(scenes.AttachScriptComponent(rootID,scriptSource.Guid,scriptSource,error),error);
    auto* script=Script(*root);
    GameSproutRuntime::ScriptValue internal;internal.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;internal.SceneObject={true,scenes.GetObjectFileID(childID)};
    auto external=internal;external.SceneObject.ObjectFileID=scenes.GetObjectFileID(outside->GetInstanceID());
    auto sceneRoot=internal;sceneRoot.SceneObject.ObjectFileID=0;
    auto empty=internal;empty.SceneObject={false,0};
    Check(script->SetField("Test::_Target",external,error)&&script->SetField("Test::_Internal",internal,error)&&script->SetField("Test::_SceneRoot",sceneRoot,error)&&script->SetField("Test::_Empty",empty,error),error);
    const std::string path="Prefabs/ExternalCreation.prefab";Check(prefabs.CreatePrefab(rootID,path,error),"Create with external references failed: "+error);
    const Guid guid("ef0123456789abcdef0123456789abcd");PublishAdditional(project,assets,library,path,guid);ReadyEvent(assets,guid,path);
    PrefabInstanceInfo info;Check(prefabs.ReadInstance(rootID,info,error)&&info.HasOverrides,"Create did not retain original external references as overrides: "+error);
    Check(runtime.QueryGameObject(rootID)==root&&runtime.QueryGameObject(childID)==child&&Script(*root)==script&&script->GetFields().at("Test::_Target")==external&&
          script->GetFields().at("Test::_SceneRoot")==sceneRoot&&script->GetFields().at("Test::_Internal")==internal,"Connecting the source cleared/replaced the original scene references or object identity");
    uint64_t copyID=0;Check(prefabs.InstantiatePrefab(info.Source,0,copyID,error),error);auto* copy=runtime.QueryGameObject(copyID);auto* copied=Script(*copy);
    Check(copied&&!copied->GetFields().at("Test::_Target").SceneObject.Assigned&&!copied->GetFields().at("Test::_SceneRoot").SceneObject.Assigned&&
          !copied->GetFields().at("Test::_Empty").SceneObject.Assigned&&copied->GetFields().at("Test::_Internal").SceneObject.ObjectFileID==scenes.GetObjectFileID(copy->GetChild(0)->GetInstanceID()),
          "A new Prefab instance inherited external references or lost its internal reference");
    auto latest=Data(project,path);for(auto& node:latest["Objects"])if(node["ParentID"].asUInt64())node["Components"][0]["Scale"][2]=2.0f;
    SaveData(project,path,latest);PublishAdditional(project,assets,library,path,guid);
    Check(prefabs.RefreshSource(guid.GetGuidString(),error)&&script->GetFields().at("Test::_Target")==external&&script->GetFields().at("Test::_SceneRoot")==sceneRoot&&
          runtime.QuerySceneObjectByFileID(scene,0)==scene,"Source refresh failed to round-trip an assigned external scene-root reference: "+error);
    const auto before=Read(QString::fromStdString(project.Root+"/Assets/"+path));
    Check(!prefabs.ApplyOverrides(rootID,error)&&Read(QString::fromStdString(project.Root+"/Assets/"+path))==before,"Apply wrote external scene references into the Prefab source");
    Check(script->SetField("Test::_Target",empty,error),error);
    Check(!prefabs.ApplyOverrides(rootID,error)&&Read(QString::fromStdString(project.Root+"/Assets/"+path))==before&&script->GetFields().at("Test::_SceneRoot")==sceneRoot,
          "Apply treated assigned scene-root FileID 0 as an unassigned reference");
    Check(script->SetField("Test::_Target",external,error),error);
    Check(prefabs.RevertOverrides(rootID,error),error);
    Check(!script->GetFields().at("Test::_Target").SceneObject.Assigned&&!script->GetFields().at("Test::_SceneRoot").SceneObject.Assigned&&
          script->GetFields().at("Test::_Internal")==internal&&runtime.QueryGameObject(childID)==child&&prefabs.ReadInstance(rootID,info,error)&&!info.HasOverrides,
          "Revert did not clear external overrides while preserving internal references/identities");
}
static void CheckCreateAndConnect(TestProject& project,TestAssets& assets,TestLibrary& library,ISceneManager& scenes,IPrefabManager& prefabs,GameSproutRuntime::IGameSproutRuntime& runtime) {
    auto* scene=scenes.GetActiveScene();auto* root=scene->CreateGameObject("Created:Root");auto* child=scene->CreateGameObject("Original child",root);
    const auto rootID=root->GetInstanceID(),childID=child->GetInstanceID();const auto* transform=root->GetTransform();const auto* childTransform=child->GetTransform();
    const GameSproutRuntime::ResourceReference scriptSource{"3456789012abcdef3456789012abcdef",1};std::string error;
    Check(scenes.AttachScriptComponent(rootID,scriptSource.Guid,scriptSource,error),error);
    auto* script=Script(*root);GameSproutRuntime::ScriptValue reference;reference.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;reference.SceneObject={true,scenes.GetObjectFileID(childID)};
    Check(script->SetField("Test::_Target",reference,error),error);
    SceneSubtreeDocument before;Check(scenes.CaptureObjectDocument(rootID,before,error),error);
    const auto rootFile=scenes.GetObjectFileID(rootID),childFile=scenes.GetObjectFileID(childID);
    std::string path;Check(prefabs.CreatePrefabInDirectory(rootID,"Prefabs",path,error),"Create in Project directory failed: "+error);
    Check(path=="Prefabs/Created_Root.prefab","Directory creation did not sanitize the object filename");
    const auto full=QString::fromStdString(project.Root+"/Assets/"+path);
    Check(QFileInfo::exists(full)&&!QFileInfo::exists(full+".meta"),"CreatePrefab created Meta before AssetsManager registration");
    PrefabInstanceInfo info;Check(!prefabs.ReadInstance(rootID,info,error),"Pending creation connected before the Ready event");
    SetPosition(scenes,*child,{5,6,7});
    auto changed=Data(project,path);const auto sourceRoot=PrefabDefinition::RootID(changed);
    for(auto& node:changed["Objects"])if(node["ID"].asUInt64()!=sourceRoot)node["Components"][0]["Scale"][1]=3.0f;
    SaveData(project,path,changed);
    const Guid createdGuid("abcdef0123456789abcdef0123456789");PublishAdditional(project,assets,library,path,createdGuid);ReadyEvent(assets,createdGuid,path);
    Check(runtime.QueryGameObject(rootID)==root&&runtime.QueryGameObject(childID)==child&&root->GetTransform()==transform&&child->GetTransform()==childTransform&&Script(*root)==script,
          "Creating and connecting replaced original Runtime node/component identities");
    Check(scenes.GetObjectFileID(rootID)==rootFile&&scenes.GetObjectFileID(childID)==childFile,"Creating and connecting changed original scene FileIDs");
    CheckReference(scenes,runtime,*root);
    Check(prefabs.ReadInstance(rootID,info,error)&&info.RootObjectID==rootID&&info.Source.Guid==createdGuid.GetGuidString()&&info.Source.LocalID==7&&info.HasOverrides,
          "Ready creation did not connect the original object to the imported main resource: "+error);
    Check(child->GetTransform()->GetLocalPosition().X==5&&child->GetTransform()->GetLocalPosition().Y==6&&child->GetTransform()->GetLocalScale().Y==3,
          "Ready creation lost edits made while importing or skipped current source refresh");
    bool namedOverride=false;for(const auto& patch:info.Overrides)if(patch.ObjectName=="Original child"&&patch.ComponentType=="GameSproutRuntime.Transform")namedOverride=true;
    Check(namedOverride,"Override DTO did not provide object/component names");
    SceneSubtreeDocument after;Check(scenes.CaptureObjectDocument(rootID,after,error),error);
    Check(before.ObjectIDs.size()==after.ObjectIDs.size(),"Creation lost component identity mappings");
    for(std::size_t i=0;i<before.ObjectIDs.size();++i)Check(before.ObjectIDs[i].SceneFileID==after.ObjectIDs[i].SceneFileID&&before.ObjectIDs[i].RuntimeObjectID==after.ObjectIDs[i].RuntimeObjectID,
          "Creation replaced a source object's existing scene identity");
    auto* duplicate=scene->CreateGameObject("Created:Root");std::string unique;
    Check(prefabs.CreatePrefabInDirectory(duplicate->GetInstanceID(),"Prefabs",unique,error)&&unique=="Prefabs/Created_Root 1.prefab","Project drag creation did not select a unique source filename");
    auto* parent=scene->CreateGameObject("Contains instance");uint64_t nestedID=0;
    Check(prefabs.InstantiatePrefab(info.Source,parent->GetInstanceID(),nestedID,error),error);
    Check(!prefabs.CreatePrefab(parent->GetInstanceID(),"Prefabs/NestedBlocked.prefab",error)&&!QFileInfo::exists(QString::fromStdString(project.Root+"/Assets/Prefabs/NestedBlocked.prefab")),
          "CreatePrefab connected a parent containing an existing Prefab instance");
    auto* lateParent=scene->CreateGameObject("Late nested parent");const auto lateID=lateParent->GetInstanceID();const std::string latePath="Prefabs/LateNested.prefab";
    Check(prefabs.CreatePrefab(lateID,latePath,error),error);Check(prefabs.InstantiatePrefab(info.Source,lateID,nestedID,error),error);
    const Guid lateGuid("bcdef0123456789abcdef0123456789a");PublishAdditional(project,assets,library,latePath,lateGuid);ReadyEvent(assets,lateGuid,latePath);
    Check(!prefabs.ReadInstance(lateID,info,error)&&QFileInfo::exists(QString::fromStdString(project.Root+"/Assets/"+latePath)),"Ready creation crossed a later Nested boundary or deleted its saved source");
    auto* deleted=scene->CreateGameObject("Deleted pending");const auto deletedID=deleted->GetInstanceID();const std::string deletedPath="Prefabs/DeletedPending.prefab";
    Check(prefabs.CreatePrefab(deletedID,deletedPath,error),error);Check(scenes.DeleteObject(deletedID,error),error);
    const Guid deletedGuid("cdef0123456789abcdef0123456789ab");PublishAdditional(project,assets,library,deletedPath,deletedGuid);ReadyEvent(assets,deletedGuid,deletedPath);
    Check(!runtime.QueryGameObject(deletedID),"Ready creation resurrected a deleted original object");
    auto* cancelled=scene->CreateGameObject("Scene switch pending");const auto cancelledID=cancelled->GetInstanceID();const std::string cancelledPath="Prefabs/SceneSwitch.prefab";
    Check(prefabs.CreatePrefab(cancelledID,cancelledPath,error),error);const auto cancelledFile=scenes.GetObjectFileID(cancelledID);
    Check(scenes.SaveSceneAs("Scenes/PendingCreation.scene",error)&&scenes.OpenScene("Scenes/PendingCreation.scene",error),error);
    const Guid cancelledGuid("def0123456789abcdef0123456789abc");PublishAdditional(project,assets,library,cancelledPath,cancelledGuid);ReadyEvent(assets,cancelledGuid,cancelledPath);
    auto* reopened=scenes.QueryObjectByFileID(cancelledFile);
    Check(reopened&&reopened->GetInstanceID()!=cancelledID&&!prefabs.ReadInstance(reopened->GetInstanceID(),info,error),"A Ready event after a scene switch connected a different scene/object generation");
}

class SchemaBehaviour final : public GameSproutRuntime::IScript {
public:
    explicit SchemaBehaviour(GameSproutRuntime::IGameObject* owner):IScript(owner){}
    const std::type_info& GetComponentType() const override{return typeid(SchemaBehaviour);}
    const char* GetTypeName() const override{return "SchemaBehaviour";}
};
static GameSproutRuntime::ScriptFieldRegistration SchemaField(const std::string& id,GameSproutRuntime::ScriptValue value) {
    GameSproutRuntime::ScriptFieldRegistration field;field.Info.ID=id;field.Info.Name=id;field.Info.Default=value;
    field.Write=[](GameSproutRuntime::IScript&,const GameSproutRuntime::ScriptValue&,const GameSproutRuntime::ScriptReferenceContext&,std::string& error){error.clear();return true;};
    field.Read=[value](const GameSproutRuntime::IScript&,GameSproutRuntime::ScriptValue& output,const GameSproutRuntime::ScriptReferenceContext&,std::string& error){output=value;error.clear();return true;};
    field.Validate=[](const auto&,const auto&,std::string& error){error.clear();return true;};return field;
}
static void CheckScriptSchemaOverrides(TestProject& project,TestAssets& assets,TestLibrary& library,ISceneManager& scenes,
    IPrefabManager& prefabs,GameSproutRuntime::IGameSproutRuntime& runtime) {
    const std::string typeID="Prefab.Schema";const Guid prefabGuid("99887766554433221100aabbccddeeff");
    const GameSproutRuntime::ResourceReference scriptSource{"66554433221100aabbccddeeff998877",1};
    GameSproutRuntime::ScriptValue removed,changed,kept,added;
    removed.Kind=GameSproutRuntime::ScriptValueKind::Boolean;
    changed.Kind=GameSproutRuntime::ScriptValueKind::Integer;changed.Integer=0;
    kept.Kind=GameSproutRuntime::ScriptValueKind::Number;kept.Number=1;
    added.Kind=GameSproutRuntime::ScriptValueKind::Boolean;added.Boolean=true;
    GameSproutRuntime::ScriptTypeRegistration oldType;oldType.Info.TypeID=typeID;oldType.Info.Name="SchemaBehaviour";
    oldType.Source=scriptSource;oldType.CppType=&typeid(SchemaBehaviour);
    oldType.Create=[](GameSproutRuntime::IGameObject* owner)->GameSproutRuntime::IScript*{return new SchemaBehaviour(owner);};
    oldType.Destroy=[](GameSproutRuntime::IScript* instance){delete instance;};
    oldType.Fields={SchemaField("Schema::Removed",removed),SchemaField("Schema::Changed",changed),SchemaField("Schema::Kept",kept)};
    std::string error;Check(runtime.RegisterScriptType(oldType,error),error);
    auto* scene=scenes.GetActiveScene();auto* original=scene->CreateGameObject("Schema source");
    Check(runtime.AddScriptComponent(original,typeID,scriptSource,1,error),error);
    const std::string path="Prefabs/Schema.prefab";Check(prefabs.SavePrefab(original->GetInstanceID(),path,error),error);
    PublishAdditional(project,assets,library,path,prefabGuid);
    const GameSproutRuntime::ResourceReference reference{prefabGuid.GetGuidString(),7};uint64_t firstID=0,secondID=0;
    Check(prefabs.InstantiatePrefab(reference,0,firstID,error)&&prefabs.InstantiatePrefab(reference,0,secondID,error),error);
    auto* first=runtime.QueryGameObject(firstID);auto* second=runtime.QueryGameObject(secondID);
    auto changedOverride=changed;changedOverride.Integer=7;auto keptOverride=kept;keptOverride.Number=5;
    Check(Script(*first)->SetField("Schema::Changed",changedOverride,error)&&Script(*first)->SetField("Schema::Kept",keptOverride,error),error);
    auto source=Data(project,path);source["Objects"][0]["Components"][0]["Scale"][1]=2.0;
    SaveData(project,path,source);PublishAdditional(project,assets,library,path,prefabGuid);
    Check(prefabs.RefreshSource(prefabGuid.GetGuidString(),error),error);
    const auto sourceBefore=Read(QString::fromStdString(project.Root+"/Assets/"+path));
    const auto annotationsBefore=scenes.GetSceneAnnotationsJson();
    GameSproutRuntime::ScriptTypeRegistration currentType=oldType;changed.Kind=GameSproutRuntime::ScriptValueKind::Number;changed.Number=7;
    currentType.Fields={SchemaField("Schema::Changed",changed),SchemaField("Schema::Kept",kept),SchemaField("Schema::Added",added)};
    Check(runtime.ReplaceScriptTypes({typeID},{currentType},error),error);
    PrefabInstanceInfo info;
    for(int read=0;read<3;++read) {
        Check(prefabs.ReadInstance(firstID,info,error)&&info.Overrides.size()==1&&info.Overrides[0].PropertyPath.find("Schema::Kept")!=std::string::npos,
            "Script declaration changes became false overrides or retained obsolete stored patches: "+error);
        Check(prefabs.ReadInstance(secondID,info,error)&&!info.HasOverrides,"Unedited instance gained overrides from removed/new/changed script declarations: "+error);
    }
    Check(Read(QString::fromStdString(project.Root+"/Assets/"+path))==sourceBefore&&scenes.GetSceneAnnotationsJson()==annotationsBefore,
        "ReadInstance rewrote source or scene while projecting script declarations");
    source=Data(project,path);source["Objects"][0]["Components"][0]["Scale"][1]=3.0;
    SaveData(project,path,source);PublishAdditional(project,assets,library,path,prefabGuid);
    Check(prefabs.RefreshSource(prefabGuid.GetGuidString(),error),error);
    Check(Script(*first)->GetFields().size()==3&&Script(*first)->GetFields().at("Schema::Kept").Number==5&&
          Script(*first)->GetFields().at("Schema::Changed").Number==7&&Script(*first)->GetFields().at("Schema::Added").Boolean&&
          prefabs.ReadInstance(secondID,info,error)&&!info.HasOverrides,"Source refresh lost valid script overrides or schema defaults: "+error);
    Check(prefabs.RevertOverrides(firstID,error)&&Script(*first)->GetFields().at("Schema::Kept").Number==1&&
          prefabs.ReadInstance(firstID,info,error)&&!info.HasOverrides,"Revert did not retain the current script declaration: "+error);
    keptOverride.Number=3;Check(Script(*first)->SetField("Schema::Kept",keptOverride,error)&&prefabs.ApplyOverrides(firstID,error),error);
    source=Data(project,path);const Json::Value* fields=nullptr;
    for(const auto& component:source["Objects"][0]["Components"])if(component["Type"]=="GameSproutRuntime.ScriptComponent")fields=&component["Fields"];
    Check(fields&&fields->size()==3&&!fields->isMember("Schema::Removed")&&(*fields)["Schema::Changed"]["Kind"]=="Number"&&
          Script(*second)->GetFields().at("Schema::Kept").Number==3&&prefabs.ReadInstance(firstID,info,error)&&!info.HasOverrides,
          "Apply did not publish current fields and propagate a valid value override: "+error);
    Check(runtime.UnregisterScriptType(typeID,error),error);
}

int main(int argc,char** argv){
    QCoreApplication app(argc,argv);GameSproutRuntime::IGameSproutRuntime* runtime=nullptr;
    GameSproutEditor::IGameSproutModule* scenesModule=nullptr;GameSproutEditor::IGameSproutModule* prefabsModule=nullptr;
    try {
        Check(argc==2,"Pass the independently built Runtime SDK directory");runtime=Export(argv[1]);Check(runtime,"Cannot load independent Runtime");
        QTemporaryDir temporary;Check(temporary.isValid(),"Temporary project unavailable");
        QDir projectRoot(temporary.path());Check(projectRoot.mkpath("Assets/Scenes")&&projectRoot.mkpath("Assets/Prefabs"),"Cannot create temporary Assets");
        TestProject project;project.Root=temporary.path().toStdString();TestAssets assets;TestLibrary library;TestEditor editor(runtime);
        scenesModule=LoadModule(SCENE_MODULE_FILE);prefabsModule=LoadModule(PREFAB_MODULE_FILE);
        auto* scenes=dynamic_cast<ISceneManager*>(scenesModule);auto* prefabs=dynamic_cast<IPrefabManager*>(prefabsModule);
        Check(scenes&&prefabs,"Production module exports have wrong interfaces");
        editor.Modules={{"Project",&project},{"AssetsManager",&assets},{"LibraryManager",&library},{"SceneManager",scenesModule},{"PrefabManager",prefabsModule}};
        GameSproutEditor::GameSproutModuleApi api(nullptr,&editor);
        TestModuleScope moduleScope(*scenesModule,*prefabsModule,api);
        for(auto* module:{scenesModule,prefabsModule})GameSproutEditor::GameSproutEditorFinal::Before(*module,api);
        for(auto* module:{scenesModule,prefabsModule})Check(GameSproutEditor::GameSproutEditorFinal::Initialize(*module,api),"Production module Initialize failed");
        for(auto* module:{scenesModule,prefabsModule})Check(GameSproutEditor::GameSproutEditorFinal::Launch(*module,api),"Production module Launch failed");
        for(auto* module:{scenesModule,prefabsModule})GameSproutEditor::GameSproutEditorFinal::Finished(*module,api);
        auto* scene=scenes->GetActiveScene();auto* source=scene->CreateGameObject("Source");auto* child=scene->CreateGameObject("Child",source);
        auto* external=scene->CreateGameObject("External target");const auto sourceID=source->GetInstanceID();
        std::string error;
        auto classifications=runtime->GetProjectSettings();
        classifications.LayerNames[3]="Characters";classifications.LayerNames[4]="Enemies";classifications.LayerNames[5]="Buildings";
        classifications.Tags.push_back({1,"Enemy"});classifications.Tags.push_back({2,"Friendly"});classifications.NextTagID=3;
        Check(runtime->SetProjectSettings(classifications,error)&&child->SetLayer(3)&&child->SetTagID(1)&&child->SetStaticFlags(GameSproutRuntime::StaticFlags::ContributeGI),error);
        const GameSproutRuntime::ResourceReference scriptSource{"3456789012abcdef3456789012abcdef",1};
        Check(scenes->AttachScriptComponent(sourceID,scriptSource.Guid,scriptSource,error),error);
        GameSproutRuntime::ScriptValue internal;internal.Kind=GameSproutRuntime::ScriptValueKind::SceneObject;internal.SceneObject={true,scenes->GetObjectFileID(child->GetInstanceID())};
        Check(Script(*source)->SetField("Test::_Target",internal,error),error);
        const std::string path="Prefabs/Thing.prefab";const QString full=projectRoot.filePath("Assets/"+QString::fromStdString(path));
        Check(prefabs->SavePrefab(sourceID,path,error),"SavePrefab failed: "+error);const auto sourceBytes=Read(full);
        Check(!prefabs->SavePrefab(sourceID,path,error)&&Read(full)==sourceBytes,"SavePrefab overwrote an existing source");
        GameSproutRuntime::ScriptValue outside=internal;outside.SceneObject.ObjectFileID=scenes->GetObjectFileID(external->GetInstanceID());
        Check(Script(*source)->SetField("Test::_Target",outside,error),error);
        CheckExportReferences(project,*scenes,*prefabs,*runtime,*source,*child,*external);
        Check(Script(*source)->SetField("Test::_Target",internal,error),error);
        Publish(project,assets,library,path);const auto metaBytes=Read(full+".meta");const auto initialData=Data(project,path);
        const GameSproutRuntime::ResourceReference reference{assets.Meta->_Guid.GetGuidString(),1};uint64_t firstID=0,secondID=0;
        Check(prefabs->InstantiatePrefab(reference,0,firstID,error),"First instantiate failed: "+error);
        Check(prefabs->InstantiatePrefab(reference,0,secondID,error),"Second instantiate failed: "+error);
        auto* first=runtime->QueryGameObject(firstID);auto* second=runtime->QueryGameObject(secondID);
        Check(first&&second&&first!=second,"Instances are not independent");CheckReference(*scenes,*runtime,*first);CheckReference(*scenes,*runtime,*second);
        const auto firstFile=scenes->GetObjectFileID(firstID),secondFile=scenes->GetObjectFileID(secondID);
        const auto firstChildID=first->GetChild(0)->GetInstanceID(),secondChildID=second->GetChild(0)->GetInstanceID();
        PrefabInstanceInfo info;Check(prefabs->ReadInstance(firstChildID,info,error),"ReadInstance failed: "+error);
        if(info.HasOverrides)for(const auto& patch:info.Overrides)std::cerr<<"Unexpected override "<<patch.SourceObjectID<<"/"<<patch.ComponentID<<patch.PropertyPath<<" before="<<patch.SourceValueJson<<" after="<<patch.InstanceValueJson<<"\n";
        Check(info.RootObjectID==firstID&&!info.HasOverrides,"Fresh instance has unexpected overrides: "+error);
        SetPosition(*scenes,*first->GetChild(0),{9,0,0});
        SetClassification(*scenes,*first->GetChild(0),4,2);
        Check(scenes->SetObjectStaticFlags(firstChildID,GameSproutRuntime::StaticFlags::NavigationStatic,GameSproutRuntime::StaticFlags::Everything,false,error),error);
        Check(prefabs->ReadInstance(firstID,info,error)&&info.HasOverrides,"Instance edit was not detected as an override");
        auto editedSource=Data(project,path);const auto sourceRoot=PrefabDefinition::RootID(editedSource);
        for(auto& node:editedSource["Objects"])if(node["ID"].asUInt64()!=sourceRoot){node["Layer"]=Json::UInt(5);node["TagID"]=Json::UInt64(0);node["StaticFlags"]=Json::UInt(64);node["Name"]="Updated child";node["Components"][0]["Position"][1]=4.0f;node["Components"][0]["Scale"][2]=3.0f;}
        SaveData(project,path,editedSource);Publish(project,assets,library,path);
        Check(prefabs->RefreshSource(reference.Guid,error),"RefreshSource failed: "+error);
        Check(runtime->QueryGameObject(firstID)==first&&runtime->QueryGameObject(secondID)==second&&first->GetChild(0)->GetInstanceID()==firstChildID&&second->GetChild(0)->GetInstanceID()==secondChildID,
              "RefreshSource rebuilt unchanged instance identities");
        Check(first->GetChild(0)->GetTransform()->GetLocalPosition().X==9&&first->GetChild(0)->GetTransform()->GetLocalPosition().Y==4&&
              first->GetChild(0)->GetTransform()->GetLocalScale().Z==3&&second->GetChild(0)->GetTransform()->GetLocalPosition().X==0&&second->GetChild(0)->GetTransform()->GetLocalPosition().Y==4&&
              first->GetChild(0)->GetName()=="Updated child","RefreshSource did not preserve overridden fields while updating other fields");
        Check(first->GetChild(0)->GetLayer()==4&&first->GetChild(0)->GetTagID()==2&&
              second->GetChild(0)->GetLayer()==5&&second->GetChild(0)->GetTagID()==0,"Source refresh lost Tag/Layer overrides");
        Check(first->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::NavigationStatic&&
              second->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::ReflectionProbeStatic,"Source refresh lost StaticFlags overrides");
        CheckReference(*scenes,*runtime,*first);CheckReference(*scenes,*runtime,*second);
        Check(prefabs->ApplyOverrides(firstID,error),"ApplyOverrides failed: "+error);
        const auto applied=Data(project,path);Check(Read(full+".meta")==metaBytes,"Apply changed the source GUID/meta");
        Check(applied["Objects"][0]["ID"]==initialData["Objects"][0]["ID"]&&applied["Objects"][1]["ID"]==initialData["Objects"][1]["ID"]&&
              applied["Objects"][0]["Components"][0]["ID"]==initialData["Objects"][0]["Components"][0]["ID"]&&
              applied["Objects"][0]["Components"][1]["ID"]==initialData["Objects"][0]["Components"][1]["ID"]&&
              applied["Objects"][1]["Components"][0]["ID"]==initialData["Objects"][1]["Components"][0]["ID"],"Apply changed existing source object/component IDs");
        Check(second->GetChild(0)->GetTransform()->GetLocalPosition().X==9&&prefabs->ReadInstance(firstID,info,error)&&!info.HasOverrides,
              "Apply did not clear first overrides or propagate source changes to the second instance");
        Check(second->GetChild(0)->GetLayer()==4&&second->GetChild(0)->GetTagID()==2&&
              applied["Objects"][1]["Layer"]==4&&applied["Objects"][1]["TagID"].asUInt64()==2,"Apply did not persist/propagate Tag/Layer overrides");
        Publish(project,assets,library,path);
        SetPosition(*scenes,*first,{11,12,13});SetPosition(*scenes,*first->GetChild(0),{99,4,0});
        Check(second->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::NavigationStatic&&
              applied["Objects"][1]["StaticFlags"].asUInt()==8,"Apply did not persist or propagate static declarations");
        SetClassification(*scenes,*first->GetChild(0),6,1);
        Check(scenes->SetObjectStaticFlags(firstChildID,GameSproutRuntime::StaticFlags::OccluderStatic,GameSproutRuntime::StaticFlags::Everything,false,error),error);
        Check(prefabs->RevertOverrides(firstID,error),"RevertOverrides failed: "+error);
        Check(first->GetTransform()->GetLocalPosition().X==11&&first->GetTransform()->GetLocalPosition().Y==12&&first->GetChild(0)->GetTransform()->GetLocalPosition().X==9&&
              prefabs->ReadInstance(firstID,info,error)&&!info.HasOverrides,"Revert did not clear overrides while preserving root placement");
        Check(first->GetChild(0)->GetLayer()==4&&first->GetChild(0)->GetTagID()==2,"Revert lost source Tag/Layer");
        Check(first->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::NavigationStatic,"Revert lost source static declarations");
        Check(scenes->Undo(error)&&first->GetChild(0)->GetStaticFlags()==GameSproutRuntime::StaticFlags::OccluderStatic&&first->GetChild(0)->GetLayer()==6&&first->GetChild(0)->GetTagID()==1&&first->GetChild(0)->GetTransform()->GetLocalPosition().X==99&&prefabs->ReadInstance(firstID,info,error)&&info.HasOverrides,
              "Undo Revert did not restore the instance override and metadata");
        Check(scenes->Redo(error)&&first->GetChild(0)->GetTransform()->GetLocalPosition().X==9,error);
        Check(QFile::rename(full,full+".missing"),"Cannot hide source fixture");
        Check(!prefabs->RefreshSource(reference.Guid,error)&&prefabs->ReadInstance(firstID,info,error)&&info.MissingSource&&info.Source.Guid==reference.Guid&&runtime->QueryGameObject(firstID)==first,
              "Missing source discarded instance contents or disconnected its source");
        Check(QFile::rename(full+".missing",full)&&prefabs->RefreshSource(reference.Guid,error)&&prefabs->ReadInstance(firstID,info,error)&&!info.MissingSource&&info.Error.empty(),"Restored source did not reconnect or clear its error");
        // Instance-only external references stay scene values across refresh, but
        // cannot be published into a standalone source definition.
        const auto externalFile=scenes->GetObjectFileID(external->GetInstanceID());
        const auto previousReference=Script(*first)->GetFields().at("Test::_Target");
        GameSproutRuntime::ScriptValue externalOverride=previousReference;externalOverride.SceneObject.ObjectFileID=externalFile;
        Check(scenes->EditObject(firstID,"External instance reference",[externalOverride](GameSproutRuntime::IGameObject& node,std::string& reason){return Script(node)->SetField("Test::_Target",externalOverride,reason);},
              [previousReference](GameSproutRuntime::IGameObject& node,std::string& reason){return Script(node)->SetField("Test::_Target",previousReference,reason);},error),error);
        editedSource=Data(project,path);
        for(auto& node:editedSource["Objects"])if(node["ID"].asUInt64()!=sourceRoot)node["Components"][0]["Scale"][0]=2.0f;
        SaveData(project,path,editedSource);Publish(project,assets,library,path);
        Check(prefabs->RefreshSource(reference.Guid,error)&&Script(*first)->GetFields().at("Test::_Target")==externalOverride&&
              runtime->QuerySceneObjectByFileID(scenes->GetActiveScene(),externalFile)==external,
              "Source refresh lost an instance-only external object reference: "+error);
        const auto beforeRejectedApply=Read(full);
        Check(!prefabs->ApplyOverrides(firstID,error)&&Read(full)==beforeRejectedApply,"Apply published an external scene reference into its source");
        Check(prefabs->RevertOverrides(firstID,error),"Revert external reference failed: "+error);CheckReference(*scenes,*runtime,*first);

        // Structural overrides remain local during source updates and all kinds
        // are removed by one Revert transaction.
        auto* local=scene->CreateGameObject("Local child",first);const auto localID=local->GetInstanceID();
        Check(prefabs->ReadInstance(localID,info,error)&&info.RootObjectID==firstID,"Added descendants must resolve their containing Prefab instance: "+error);
        uint64_t rejectedNestedID=0;
        Check(!prefabs->InstantiatePrefab(reference,localID,rejectedNestedID,error)&&!rejectedNestedID,
              "An added instance descendant bypassed the reserved Nested Prefab boundary");
        Check(scenes->AddBuiltinComponent(firstChildID,"GameSproutRuntime.Image",error),error);
        const auto removedScriptID=Script(*first)->GetScriptInstanceID();
        Check(scenes->RemoveScriptComponent(firstID,removedScriptID,error),error);
        Check(prefabs->ReadInstance(firstID,info,error)&&info.HasOverrides,"Structural changes were not recorded as overrides");
        bool addedObject=false,addedComponent=false,removedComponent=false;
        for(const auto& patch:info.Overrides){addedObject=addedObject||patch.Kind==PrefabOverrideKind::AddedObject;addedComponent=addedComponent||patch.Kind==PrefabOverrideKind::AddedComponent;removedComponent=removedComponent||patch.Kind==PrefabOverrideKind::RemovedComponent;}
        Check(addedObject&&addedComponent&&removedComponent,"Override summary omitted an object/component addition/removal");
        editedSource=Data(project,path);
        for(auto& node:editedSource["Objects"])if(node["ID"].asUInt64()!=sourceRoot)node["Components"][0]["Scale"][1]=2.0f;
        SaveData(project,path,editedSource);Publish(project,assets,library,path);
        Check(prefabs->RefreshSource(reference.Guid,error)&&runtime->QueryGameObject(localID)==local&&
              first->GetChild(0)->GetComponent<GameSproutRuntime::IImageComponent>()&&!Script(*first)&&first->GetChild(0)->GetTransform()->GetLocalScale().Y==2,
              "Source update lost structural overrides: "+error);
        Check(prefabs->RevertOverrides(firstID,error)&&!runtime->QueryGameObject(localID)&&!first->GetChild(0)->GetComponent<GameSproutRuntime::IImageComponent>()&&
              Script(*first)&&Script(*first)->GetScriptInstanceID()==removedScriptID&&prefabs->ReadInstance(firstID,info,error)&&!info.HasOverrides,
              "Revert failed to remove structural overrides or restore script identity: "+error);
        CheckReference(*scenes,*runtime,*first);CheckReference(*scenes,*runtime,*second);
        auto* appliedChild=scene->CreateGameObject("Applied child",first);const auto appliedChildID=appliedChild->GetInstanceID();
        Check(prefabs->ApplyOverrides(firstID,error),"Apply added object failed: "+error);
        Check(runtime->QueryGameObject(appliedChildID)==appliedChild,"Apply rebuilt the added object's existing scene identity");
        Check(first->GetChildCount()==2&&second->GetChildCount()==2&&second->FindChild("Applied child"),
              "Apply did not propagate added object: first children="+std::to_string(first->GetChildCount())+" second children="+std::to_string(second->GetChildCount()));
        Check(prefabs->ReadInstance(appliedChildID,info,error),"Read applied added object failed: "+error);
        if(info.HasOverrides)for(const auto& patch:info.Overrides)std::cerr<<"Residual Apply override "<<patch.SourceObjectID<<"/"<<patch.ComponentID<<patch.PropertyPath<<" before="<<patch.SourceValueJson<<" after="<<patch.InstanceValueJson<<"\n";
        Check(runtime->QueryGameObject(appliedChildID)==appliedChild&&first->GetChildCount()==2&&second->GetChildCount()==2&&
              second->FindChild("Applied child")&&prefabs->ReadInstance(appliedChildID,info,error)&&!info.HasOverrides,
              "Apply did not canonicalize and propagate a new object while keeping its scene identity");
        const auto structuralSource=Data(project,path);Check(structuralSource["Objects"].size()==3,"Apply did not persist the added source object");
        std::set<uint64_t> structuralIDs;
        for(const auto& node:structuralSource["Objects"]){structuralIDs.insert(node["ID"].asUInt64());for(const auto& component:node["Components"])structuralIDs.insert(component["ID"].asUInt64());}
        Check(structuralIDs.size()==7&&*structuralIDs.rbegin()<PrefabDefinition::InstanceBit,"Apply left instance-only or duplicate identities in the source");
        Publish(project,assets,library,path);
        Check(scenes->SaveSceneAs("Scenes/Instances.scene",error),error);const auto annotations=scenes->GetSceneAnnotationsJson();
        Check(scenes->OpenScene("Scenes/Instances.scene",error),error);first=scenes->QueryObjectByFileID(firstFile);second=scenes->QueryObjectByFileID(secondFile);
        Check(first&&second&&scenes->GetSceneAnnotationsJson()==annotations&&prefabs->ReadInstance(first->GetChild(0)->GetInstanceID(),info,error)&&info.Source.Guid==reference.Guid,
              "Scene save/reopen lost Prefab connection records");CheckReference(*scenes,*runtime,*first);CheckReference(*scenes,*runtime,*second);
        uint64_t thirdID=0;const auto count=Parse(QByteArray::fromStdString(scenes->GetSceneAnnotationsJson()))["PrefabInstances"].size();
        Check(prefabs->InstantiatePrefab(reference,0,thirdID,error),error);const auto thirdFile=scenes->GetObjectFileID(thirdID);
        Check(scenes->Undo(error)&&!scenes->QueryObjectByFileID(thirdFile)&&Parse(QByteArray::fromStdString(scenes->GetSceneAnnotationsJson()))["PrefabInstances"].size()==count,
              "Undo instance insertion left nodes or Prefab records");
        Check(scenes->Redo(error)&&scenes->QueryObjectByFileID(thirdFile)&&prefabs->ReadInstance(scenes->QueryObjectByFileID(thirdFile)->GetInstanceID(),info,error),error);
        CheckExternalCreateAndConnect(project,assets,library,*scenes,*prefabs,*runtime);
        CheckCreateAndConnect(project,assets,library,*scenes,*prefabs,*runtime);
        CheckScriptSchemaOverrides(project,assets,library,*scenes,*prefabs,*runtime);
        moduleScope.Stop();prefabsModule=nullptr;scenesModule=nullptr;delete runtime;runtime=nullptr;
        std::cout<<"PASS production Prefab/Scene modules: source save, independent instances, references, source refresh, overrides, Apply/Revert, missing source, scene persistence and Undo/Redo\n";return 0;
    } catch(const std::exception& exception){delete runtime;std::cerr<<"FAIL "<<exception.what()<<"\n";return 1;}
}

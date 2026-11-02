#pragma once
#include "ISceneManager.h"
#include "IPrefabManager.h"
#include "IPrefabStage.h"
#include "ISelectionManager.h"
#include <algorithm>
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
class TestSelection final : public ISelectionManager {
public:
    SceneObjectSelection Scene;
    std::vector<ProjectAssetSelection> Assets;
    std::vector<ISelectionListener*> Listeners;
    std::vector<ProjectAssetSelection> GetAssetSelection() const override{return Assets;}
    void SetAssetSelection(const std::vector<ProjectAssetSelection>& selection) override {Assets=selection;if(!selection.empty())Scene={};}
    SceneObjectSelection GetSceneObjectSelection() const override{return Scene;}
    void SetSceneObjectSelection(const SceneObjectSelection& selection) override {Scene=selection;if(selection.ObjectID)Assets.clear();}
    bool RegisterSelectionListener(ISelectionListener* listener) override {Listeners.push_back(listener);return true;}
    void UnregisterSelectionListener(ISelectionListener* listener) override {std::erase(Listeners,listener);}
    bool Initialize(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    bool Launch(GameSproutEditor::GameSproutModuleApi*) override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*) override{}
};
static GameSproutEditor::IGameSproutModule* LoadModule(const char* path){
#if defined(_WIN32)
    const auto library=LoadLibraryA(path);Check(library,"Cannot load module");
    auto entry=reinterpret_cast<GameSproutEditor::IGameSproutModule*(*)()>(GetProcAddress(library,GameSproutModuleEntryName));
#else
    void* library=dlopen(path,RTLD_NOW|RTLD_LOCAL);const char* reason=library?nullptr:dlerror();Check(library,reason?reason:"Cannot load module");
    auto entry=reinterpret_cast<GameSproutEditor::IGameSproutModule*(*)()>(dlsym(library,GameSproutModuleEntryName));
#endif
    Check(entry,"Module export missing");return entry();
}
static void Pump(){for(int index=0;index<4;++index)QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);}
static void SetPosition(ISceneManager& scenes,uint64_t objectID,GameSproutRuntime::Vector3 value){
    auto* target=scenes.QueryObjectByFileID(scenes.GetObjectFileID(objectID));Check(target,"Position target missing");
    const auto old=target->GetTransform()->GetLocalPosition();std::string error;
    Check(scenes.EditObject(objectID,"Set position",[value](GameSproutRuntime::IGameObject& node,std::string&){node.GetTransform()->SetLocalPosition(value);return true;},
        [old](GameSproutRuntime::IGameObject& node,std::string&){node.GetTransform()->SetLocalPosition(old);return true;},error),error);
}
static void SetScale(ISceneManager& scenes,uint64_t objectID,GameSproutRuntime::Vector3 value){
    auto* target=scenes.QueryObjectByFileID(scenes.GetObjectFileID(objectID));Check(target,"Scale target missing");
    const auto old=target->GetTransform()->GetLocalScale();std::string error;
    Check(scenes.EditObject(objectID,"Set scale",[value](GameSproutRuntime::IGameObject& node,std::string&){node.GetTransform()->SetLocalScale(value);return true;},
        [old](GameSproutRuntime::IGameObject& node,std::string&){node.GetTransform()->SetLocalScale(old);return true;},error),error);
}

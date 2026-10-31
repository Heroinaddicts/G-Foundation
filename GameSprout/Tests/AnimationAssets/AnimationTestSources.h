#pragma once
class MutableMeta final:public IAssetMeta {
public:
    MutableMeta(eAssetImporterType type,Guid guid,std::string path,std::string root):IAssetMeta(type,guid),Path(std::move(path)),Root(std::move(root)){}
    std::string Path,Root,Identities="[]";Json::Value Settings{Json::objectValue};eAssetImportState State=eAssetImportState::Pending;IAssetImportSettings Empty;
    std::vector<IAsset*> GetSubAssets()const override{return {};}
    eAssetImportState GetImportState()const override{return State;}std::string GetImportError()const override{return {};}
    const std::string& GetPath()const override{return Path;}IAssetImportSettings* GetImportSettings()const override{return const_cast<IAssetImportSettings*>(&Empty);}
    bool SerializeMeta(std::string& text)const override {
        Json::Value value;value["Version"]=1;value["Guid"]=_Guid.GetGuidString();value["ImporterType"]=AssetMetaFormat::ImporterTypeName(_ImporterType);
        value["Path"]=Path;value["ImportSettings"]=Settings;AssetArtifactIO::Parse(Identities,value["SubAssets"]);text=AssetArtifactIO::JsonText(value);return true;
    }
    bool SetSubAssets(const std::string& identities)override{Identities=identities;return true;}
    bool SaveMeta()override {std::string text,error;SerializeMeta(text);return AssetArtifactIO::Write(QString::fromStdString(Root+"/Assets/"+Path+".meta"),QByteArray::fromStdString(text),error);}
};
class Sources final:public IAssetsManager {
public:
    std::map<std::string,MutableMeta*> Metas;
    IAssetMeta* QueryAsset(const Guid& guid)const override{const auto found=Metas.find(guid.GetGuidString());return found==Metas.end()?nullptr:found->second;}
    std::vector<IAssetMeta*> GetAssetMetas()const override{std::vector<IAssetMeta*> result;for(const auto& entry:Metas)result.push_back(entry.second);return result;}
    int Reimports=0;
    bool ReimportAsset(const Guid&)override{++Reimports;return true;}bool ApplyImportSettings(const Guid& guid,const std::string& settings,const std::string& expected,std::string& error)override {
        const auto found=Metas.find(guid.GetGuidString());Json::Value current,source;
        if(found==Metas.end()||!AssetArtifactIO::Parse(expected,current)||current!=found->second->Settings||!AssetArtifactIO::Parse(settings,source)){error="Source settings changed";return false;}
        found->second->Settings=source;++Reimports;return true;
    }
    bool LoadMaterialSource(const Guid&,std::string&,std::string&)const override{return false;}bool SaveMaterialSource(const Guid&,const std::string&,const std::string&,std::string&)override{return false;}
    bool ExtractMaterial(const Guid&,uint64_t,const std::string&,std::string&,std::string&)override{return false;}
    std::vector<IAssetMeta*> GetAssetsInDirectory(const std::string&)const override{return {};}
    bool RegisterAssetListener(IAssetListener*)override{return true;}void UnregisterAssetListener(IAssetListener*)override{}
private:
    bool Initialize(GameSproutEditor::GameSproutModuleApi*)override{return true;}bool Launch(GameSproutEditor::GameSproutModuleApi*)override{return true;}
    void Release(GameSproutEditor::GameSproutModuleApi*)override{}
};

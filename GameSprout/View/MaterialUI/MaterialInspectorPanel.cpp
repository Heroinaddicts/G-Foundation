#include "MaterialInspectorPanel.h"
#include "MaterialInspector.h"
#include "IAssetsManager.h"
#include "ILibraryManager.h"
#include "ui_MaterialInspectorPanel.h"
#include "../../Module/Shared/MaterialDefinition.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <set>
#include <utility>

MaterialInspectorPanel::MaterialInspectorPanel(const InspectorServices& services,QWidget* parent)
    : InspectorPanel(services,parent),_Ui(new Ui::MaterialInspectorPanelForm()) {
    _Ui->setupUi(this);
    _Ui->materialInspector->Changed=[this]{if(Changed)Changed();};
}
MaterialInspectorPanel::~MaterialInspectorPanel(){Deactivate();delete _Ui;}
void MaterialInspectorPanel::SelectSource(const std::string& guid) {
    if(RequestLock)RequestLock(false);
    if(RequestSelection)RequestSelection({guid,0,false});
}
bool MaterialInspectorPanel::Load(const InspectorTarget& target,bool changed,std::string& error) {
    error.clear();
    if(target.Scene.ObjectID||target.Assets.size()!=1||!_Services.Assets||!_Services.Library) {
        error="请选择一个材质资源。";return false;
    }
    const auto& selected=target.Assets.front();
    IAssetMeta* meta=_Services.Assets->QueryAsset(Guid(selected.GuidString));
    uint64_t localID=selected.IsSubAsset?selected.LocalID:0;
    if(meta&&!selected.IsSubAsset&&meta->_ImporterType==eAssetImporterType::Material) {
        AssetImportManifest manifest;
        if(_Services.Library->GetManifest(meta->_Guid,manifest))for(const auto& resource:manifest.Assets)
            if(resource.Type==eAssetType::Material&&resource.SourceKey=="main"){localID=resource.LocalID;break;}
    }
    IAsset* asset=meta&&localID?_Services.Library->QueryAsset(meta->_Guid,localID):nullptr;
    if(!meta||(selected.IsSubAsset?(!asset||asset->GetAssetType()!=eAssetType::Material):meta->_ImporterType!=eAssetImporterType::Material)) {
        error="材质资源已删除或尚未完成导入。";return false;
    }
    const bool embedded=meta->_ImporterType!=eAssetImporterType::Material;
    _AssetDependencies.clear();
    std::string source;Json::Value document;
    if(embedded) {
        if(!MaterialDefinition::ReadPreparedMaterial(_Services.Library,meta->_Guid,localID,document,error))return false;
        const Json::Value external=document["ExternalMaterial"];
        std::set<std::string> visited;
        while(document["ExternalMaterial"].isObject()) {
            const auto reference=document["ExternalMaterial"];
            if(!MaterialDefinition::Reference(reference)){error="材质外部映射包含无效的 GUID 或 LocalID。";return false;}
            const auto guid=reference["Guid"].asString();
            _AssetDependencies.insert(guid);
            const auto key=guid+":"+std::to_string(reference["LocalID"].asUInt64());
            if(!visited.insert(key).second||visited.size()>16){error="材质外部映射存在循环。";return false;}
            if(!MaterialDefinition::ReadPreparedMaterial(_Services.Library,Guid(guid),reference["LocalID"].asUInt64(),document,error))return false;
        }
        if(external.isObject())document["ExternalMaterial"]=external;
    } else if(!_Services.Assets->LoadMaterialSource(meta->_Guid,source,error)||!AssetArtifactIO::Parse(source,document)) {
        if(error.empty())error="无法读取材质源文件。";return false;
    }
    const auto& shader=std::as_const(document)["Shader"];
    if(shader.isObject()&&shader["Guid"].isString())
        _AssetDependencies.insert(shader["Guid"].asString());
    const bool reload=changed||!_Loaded||target.Assets!=_Target.Assets||!HasPendingChanges();
    if(reload) {
        auto* inspector=_Ui->materialInspector;
        inspector->Bind(_Services.Assets,_Services.Library,_Services.Selection);
        QString name=asset?QString::fromStdString(asset->GetName()):QStringLiteral("Material");
        name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),QStringLiteral("_"));
        if(name.isEmpty())name=QStringLiteral("Material");
        const auto directory=QFileInfo(QString::fromStdString(meta->GetPath())).path();
        const auto suggested=(directory==QStringLiteral(".")?QString():directory+"/")+name+".mat";
        inspector->SetContext(selected.GuidString+":"+std::to_string(localID),embedded,suggested.toStdString());
        inspector->Load(document);
        Json::Value normalized;std::string snapshotError;
        _BaselineSettings=AssetArtifactIO::JsonText(inspector->Snapshot(normalized,snapshotError)?normalized:document);
        _BaselineSource=source;
    } else if(source!=_BaselineSource) {
        error="材质源文件已被外部修改。草稿已保留，请恢复后重新编辑。";
    }
    _Target=target;_Loaded=true;
    _Presentation={};
    _Presentation.Name=asset?QString::fromStdString(asset->GetName()):QFileInfo(QString::fromStdString(meta->GetPath())).fileName();
    _Presentation.Type=embedded?QStringLiteral("Material · 内嵌材质"):QStringLiteral("Material · 独立材质");
    const auto state=meta->GetImportState();
    _Presentation.State=state==eAssetImportState::Ready?QStringLiteral("就绪"):state==eAssetImportState::Failed?QStringLiteral("导入失败"):
        state==eAssetImportState::Pending?QStringLiteral("待导入"):QStringLiteral("无需导入");
    _Presentation.Path="Assets/"+QString::fromStdString(meta->GetPath());
    _Presentation.Guid=QString::fromStdString(selected.GuidString);
    _Presentation.LocalID=selected.IsSubAsset?QString::number(localID):QStringLiteral("源文件");
    _Presentation.Editable=!embedded;_Presentation.ApplyText=QStringLiteral("保存材质");
    auto& diagnostics=_Presentation.Diagnostics;
    if(asset){diagnostics["ArtifactPath"]=asset->GetArtifactPath();diagnostics["DataSize"]=Json::UInt64(asset->GetDataSize());diagnostics["DataVersion"]=asset->GetDataVersion();}
    diagnostics["Guid"]=selected.GuidString;diagnostics["LocalID"]=Json::UInt64(localID);
    if(document["ExternalMaterial"].isObject())diagnostics["ExternalMaterial"]=document["ExternalMaterial"];
    if(document["UnresolvedTextures"].isArray()&&!document["UnresolvedTextures"].empty())diagnostics["未找到的贴图"]=document["UnresolvedTextures"];
    _Ui->materialInspector->SetDiagnostics(diagnostics);
    const std::string guid=selected.GuidString;
    _Ui->materialInspector->Extract=[this,guid,localID](const std::string& path,std::string& failure) {
        std::string created;
        if(!_Services.Assets->ExtractMaterial(Guid(guid),localID,path,created,failure))return false;
        SelectSource(created);return true;
    };
    _Ui->materialInspector->OpenExternal=[this](const std::string& guid){SelectSource(guid);};
    if(error.empty()&&!meta->GetImportError().empty())error=meta->GetImportError();
    return true;
}
bool MaterialInspectorPanel::HasPendingChanges() const {
    if(!_Loaded||!_Presentation.Editable)return false;
    Json::Value draft;std::string error;
    return !_Ui->materialInspector->Snapshot(draft,error)||AssetArtifactIO::JsonText(draft)!=_BaselineSettings;
}
bool MaterialInspectorPanel::Validate(std::string& error) const {
    error.clear();if(!_Loaded){error="未加载材质。";return false;}
    if(!_Presentation.Editable)return true;
    Json::Value document;return _Ui->materialInspector->Read(document,error);
}
bool MaterialInspectorPanel::Apply(std::string& error) {
    if(!_Loaded||!_Presentation.Editable||_Target.Assets.size()!=1){error="当前材质不可编辑。";return false;}
    Json::Value document;
    if(!_Ui->materialInspector->Read(document,error)||!_Services.Assets->SaveMaterialSource(Guid(_Target.Assets.front().GuidString),
        AssetArtifactIO::JsonText(document),_BaselineSource,error))return false;
    return Revert(error);
}
bool MaterialInspectorPanel::Revert(std::string& error) {
    if(!_Loaded){error="未加载材质。";return false;}
    const auto target=_Target;
    return Load(target,true,error);
}
void MaterialInspectorPanel::Deactivate() {
    _Ui->materialInspector->Deactivate();
    _Loaded=false;_Target={};_Presentation={};_BaselineSettings.clear();_BaselineSource.clear();_AssetDependencies.clear();
}
void MaterialInspectorPanel::SetDebugMode(bool debug){_Ui->materialInspector->SetDebugMode(debug);}

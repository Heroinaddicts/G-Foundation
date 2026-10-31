#include "InspectorUI.h"
#include "MaterialInspector.h"
#include "ui_InspectorUI.h"
#include "../../Module/Shared/MaterialDefinition.h"
#include "../../Module/Shared/AssetArtifactIO.h"
#include <set>
#include <QFileInfo>
#include <QRegularExpression>

void InspectorUI::RefreshMaterial(IAssetMeta * meta,uint64_t localID,bool changed) {
    const bool embedded=meta->_ImporterType!=eAssetImporterType::Material;
    std::string source,error;
    Json::Value document;
    if (embedded) {
        if (!MaterialDefinition::ReadPreparedMaterial(_Library,meta->_Guid,localID,document,error)) {
            Clear(QStringLiteral("无法读取材质参数：")+QString::fromStdString(error));return;
        }
        const Json::Value external=document["ExternalMaterial"];
        std::set<std::string> visited;
        while (document["ExternalMaterial"].isObject()) {
            const Json::Value reference=document["ExternalMaterial"];
            if (!MaterialDefinition::Reference(reference)) { Clear(QStringLiteral("材质外部映射包含无效的 GUID 或 LocalID。"));return; }
            const std::string key=reference["Guid"].asString()+":"+reference["LocalID"].asString();
            if (!visited.insert(key).second || visited.size()>16) { Clear(QStringLiteral("材质外部映射存在循环。"));return; }
            if (!MaterialDefinition::ReadPreparedMaterial(_Library,Guid(reference["Guid"].asString()),reference["LocalID"].asUInt64(),document,error)) {
                Clear(QStringLiteral("无法读取已提取的材质：")+QString::fromStdString(error));return;
            }
        }
        if (external.isObject()) document["ExternalMaterial"]=external;
    } else if (!_Assets->LoadMaterialSource(meta->_Guid,source,error) || !AssetArtifactIO::Parse(source,document)) {
        Clear(QStringLiteral("无法读取材质源文件：")+QString::fromStdString(error));return;
    }
    const bool reload=changed || !_MaterialMode || _ActivePanel!=_Material || !HasPendingChanges();
    IAsset * asset=localID?_Library->QueryAsset(meta->_Guid,localID):nullptr;
    if (reload) {
        _Material->Bind(_Assets,_Library,_Selection);
        QString name=asset?QString::fromStdString(asset->GetName()):QStringLiteral("Material");
        name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),QStringLiteral("_"));
        if (name.isEmpty()) name=QStringLiteral("Material");
        const QString directory=QFileInfo(QString::fromStdString(meta->GetPath())).path();
        const QString suggested=(directory==QStringLiteral(".")?QString():directory+"/")+name+".mat";
        _Material->SetContext(_Inspected.front().GuidString+":"+std::to_string(localID),embedded,suggested.toStdString());
        _Material->Load(document);
        Json::Value normalized;
        if (_Material->Snapshot(normalized,error)) _BaselineSettings=AssetArtifactIO::JsonText(normalized);
        else _BaselineSettings=AssetArtifactIO::JsonText(document);
        _BaselineMaterialSource=source;
        _OperationError.clear();
    } else if (source!=_BaselineMaterialSource) {
        _OperationError=QStringLiteral("材质源文件已被外部修改。草稿已保留，请恢复后重新编辑。");
    }
    _MaterialMode=true;_ActivePanel=_Material;_Editable=!embedded;
    _Ui->typeLabel->setText(embedded?QStringLiteral("Material · 内嵌材质"):QStringLiteral("Material · 独立材质"));
    if (asset) _Ui->assetNameLabel->setText(QString::fromStdString(asset->GetName()));
    Json::Value diagnostics(Json::objectValue);
    if (asset) {
        diagnostics["ArtifactPath"]=asset->GetArtifactPath();diagnostics["DataSize"]=Json::UInt64(asset->GetDataSize());
        diagnostics["DataVersion"]=asset->GetDataVersion();
    }
    diagnostics["Guid"]=_Inspected.front().GuidString;
    diagnostics["LocalID"]=Json::UInt64(localID);
    if (document["ExternalMaterial"].isObject()) diagnostics["ExternalMaterial"]=document["ExternalMaterial"];
    if (document["UnresolvedTextures"].isArray() && !document["UnresolvedTextures"].empty()) diagnostics["未找到的贴图"]=document["UnresolvedTextures"];
    _Material->SetDiagnostics(diagnostics);
    const std::string sourceGuid=_Inspected.front().GuidString;
    _Material->Extract=[this,sourceGuid,localID](const std::string & path,std::string & extractError) {
        std::string newGuid;
        if (!_Assets->ExtractMaterial(Guid(sourceGuid),localID,path,newGuid,extractError)) return false;
        _Ui->lockButton->setChecked(false);
        _Selection->SetAssetSelection({{newGuid,0,false}});
        _Dirty=true;
        return true;
    };
    _Material->OpenExternal=[this](const std::string & guid) {
        _Ui->lockButton->setChecked(false);
        _Selection->SetAssetSelection({{guid,0,false}});
        _Dirty=true;
    };
    for (const auto & entry:_Panels) entry.second->hide();
    _Material->show();_Ui->textureHost->show();_Ui->infoHost->hide();
    _Ui->contentStack->setCurrentIndex(1);
    if (!meta->GetImportError().empty()) _OperationError=QString::fromStdString(meta->GetImportError());
    UpdateButtons();
}
bool InspectorUI::ApplyMaterialChanges() {
    if (!_Editable || !_Material || _Inspected.size()!=1) return false;
    Json::Value document;std::string error;
    if (!_Material->Read(document,error) || !_Assets->SaveMaterialSource(Guid(_Inspected.front().GuidString),
        AssetArtifactIO::JsonText(document),_BaselineMaterialSource,error)) {
        _OperationError=QString::fromStdString(error);UpdateButtons();return false;
    }
    RevertMaterialChanges();
    return true;
}
void InspectorUI::RevertMaterialChanges() {
    if (!_Material || !_Editable || _Inspected.size()!=1) return;
    std::string source,error;Json::Value document;
    if (!_Assets->LoadMaterialSource(Guid(_Inspected.front().GuidString),source,error) || !AssetArtifactIO::Parse(source,document)) {
        _OperationError=QString::fromStdString(error);UpdateButtons();return;
    }
    _Material->Load(document);
    Json::Value normalized;
    _BaselineSettings=_Material->Snapshot(normalized,error)?AssetArtifactIO::JsonText(normalized):AssetArtifactIO::JsonText(document);
    _BaselineMaterialSource=source;_OperationError.clear();_Dirty=true;UpdateButtons();
}

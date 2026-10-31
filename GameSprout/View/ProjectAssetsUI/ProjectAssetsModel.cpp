#include "ProjectAssetsModel.h"
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <algorithm>
#include <set>
#include "../Shared/AssetDragPayload.h"

namespace {
QString ImporterName(eAssetImporterType type) {
    switch (type) {
    case eAssetImporterType::Unknown: return QStringLiteral("Unknown");
    case eAssetImporterType::Texture: return QStringLiteral("Texture");
    case eAssetImporterType::Audio: return QStringLiteral("Audio");
    case eAssetImporterType::Model: return QStringLiteral("Model");
    case eAssetImporterType::Font: return QStringLiteral("Font");
    case eAssetImporterType::Video: return QStringLiteral("Video");
    case eAssetImporterType::Text: return QStringLiteral("Text");
    case eAssetImporterType::Shader: return QStringLiteral("Shader");
    case eAssetImporterType::ComputeShader: return QStringLiteral("ComputeShader");
    case eAssetImporterType::ShaderInclude: return QStringLiteral("ShaderInclude");
    case eAssetImporterType::Script: return QStringLiteral("Script");
    case eAssetImporterType::Mesh: return QStringLiteral("Mesh");
    case eAssetImporterType::Material: return QStringLiteral("Material");
    case eAssetImporterType::Scene: return QStringLiteral("Scene");
    case eAssetImporterType::AnimationClip: return QStringLiteral("AnimationClip");
    case eAssetImporterType::AnimatorController: return QStringLiteral("AnimatorController");
    case eAssetImporterType::AnimatorOverrideController: return QStringLiteral("AnimatorOverrideController");
    case eAssetImporterType::AvatarMask: return QStringLiteral("AvatarMask");
    case eAssetImporterType::AudioMixer: return QStringLiteral("AudioMixer");
    case eAssetImporterType::PhysicsMaterial: return QStringLiteral("PhysicsMaterial");
    case eAssetImporterType::PhysicsMaterial2D: return QStringLiteral("PhysicsMaterial2D");
    case eAssetImporterType::RenderTexture: return QStringLiteral("RenderTexture");
    case eAssetImporterType::Cubemap: return QStringLiteral("Cubemap");
    case eAssetImporterType::SpriteAtlas: return QStringLiteral("SpriteAtlas");
    case eAssetImporterType::TerrainData: return QStringLiteral("TerrainData");
    case eAssetImporterType::TerrainLayer: return QStringLiteral("TerrainLayer");
    case eAssetImporterType::ShaderVariantCollection: return QStringLiteral("ShaderVariantCollection");
    case eAssetImporterType::DataAsset: return QStringLiteral("DataAsset");
    case eAssetImporterType::LightingSettings: return QStringLiteral("LightingSettings");
    case eAssetImporterType::LightmapParameters: return QStringLiteral("LightmapParameters");
    case eAssetImporterType::Folder: return QStringLiteral("Folder");
    case eAssetImporterType::Prefab: return QStringLiteral("Prefab");
    }
    return QStringLiteral("Unknown");
}
QString ResourceName(eAssetType type) {
    switch (type) {
    case eAssetType::Unknown: return QStringLiteral("Unknown");
    case eAssetType::Texture: return QStringLiteral("Texture");
    case eAssetType::Mesh: return QStringLiteral("Mesh");
    case eAssetType::Model: return QStringLiteral("Model");
    case eAssetType::Material: return QStringLiteral("Material");
    case eAssetType::Shader: return QStringLiteral("Shader");
    case eAssetType::ComputeShader: return QStringLiteral("ComputeShader");
    case eAssetType::Font: return QStringLiteral("Font");
    case eAssetType::RenderTexture: return QStringLiteral("RenderTexture");
    case eAssetType::Cubemap: return QStringLiteral("Cubemap");
    case eAssetType::SpriteAtlas: return QStringLiteral("SpriteAtlas");
    case eAssetType::Sprite: return QStringLiteral("Sprite");
    case eAssetType::Skeleton: return QStringLiteral("Skeleton");
    case eAssetType::Avatar: return QStringLiteral("Avatar");
    case eAssetType::AnimationClip: return QStringLiteral("AnimationClip");
    case eAssetType::AnimatorController: return QStringLiteral("AnimatorController");
    case eAssetType::AnimatorOverrideController: return QStringLiteral("AnimatorOverrideController");
    case eAssetType::AvatarMask: return QStringLiteral("AvatarMask");
    case eAssetType::Audio: return QStringLiteral("Audio");
    case eAssetType::AudioMixer: return QStringLiteral("AudioMixer");
    case eAssetType::Video: return QStringLiteral("Video");
    case eAssetType::Text: return QStringLiteral("Text");
    case eAssetType::Script: return QStringLiteral("Script");
    case eAssetType::DataAsset: return QStringLiteral("DataAsset");
    case eAssetType::Scene: return QStringLiteral("Scene");
    case eAssetType::Prefab: return QStringLiteral("Prefab");
    case eAssetType::PhysicsMaterial: return QStringLiteral("PhysicsMaterial");
    case eAssetType::PhysicsMaterial2D: return QStringLiteral("PhysicsMaterial2D");
    case eAssetType::TerrainData: return QStringLiteral("TerrainData");
    case eAssetType::TerrainLayer: return QStringLiteral("TerrainLayer");
    case eAssetType::ShaderVariantCollection: return QStringLiteral("ShaderVariantCollection");
    case eAssetType::LightingSettings: return QStringLiteral("LightingSettings");
    case eAssetType::LightmapParameters: return QStringLiteral("LightmapParameters");
    case eAssetType::ShaderInclude: return QStringLiteral("ShaderInclude");
    case eAssetType::Folder: return QStringLiteral("Folder");
    }
    return QStringLiteral("Unknown");
}
QIcon Icon(const QString & type) {
    QString name = "data";
    if (type == "Folder") name = "folder";
    else if (type == "Model" || type == "Mesh" || type == "Skeleton" || type == "Avatar") name = "model";
    else if (type == "Texture" || type == "Sprite" || type == "Cubemap") name = "image";
    else if (type == "Material") name = "material";
    else if (type == "Audio" || type == "Video" || type == "AnimationClip") name = "media";
    return QIcon(":/ProjectAssets/" + name + ".svg");
}
}

ProjectAssetsModel::ProjectAssetsModel(QObject * parent) : QStandardItemModel(parent) {}

Qt::ItemFlags ProjectAssetsModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags result=QStandardItemModel::flags(index) & ~Qt::ItemIsDropEnabled & ~Qt::ItemIsDragEnabled;
    if(index.isValid() && index.column()==0 && !index.data(FolderRole).toBool() &&
        !index.data(PlaceholderRole).toBool() && Guid(index.data(GuidRole).toString().toStdString()).IsValid())result|=Qt::ItemIsDragEnabled;
    return result;
}
QStringList ProjectAssetsModel::mimeTypes() const {return {AssetDragPayload::MimeType()};}
Qt::DropActions ProjectAssetsModel::supportedDragActions() const {return Qt::CopyAction;}
QMimeData* ProjectAssetsModel::mimeData(const QModelIndexList& indexes) const {
    std::vector<ProjectAssetSelection> values;
    std::set<QString> keys;
    for(const QModelIndex& selected:indexes) {
        const QModelIndex index=selected.siblingAtColumn(0);
        if(!(flags(index)&Qt::ItemIsDragEnabled) || !keys.insert(index.data(KeyRole).toString()).second)continue;
        values.push_back({index.data(GuidRole).toString().toStdString(),index.data(LocalIDRole).toULongLong(),index.data(SubAssetRole).toBool()});
    }
    return AssetDragPayload::Encode(values);
}

QStandardItem * ProjectAssetsModel::EnsureFolder(const QString & path) {
    if (_Folders.contains(path)) return _Folders.value(path);
    const QString parentPath = QFileInfo(path).path() == "." ? "" : QFileInfo(path).path();
    QStandardItem * parent = EnsureFolder(parentPath);
    auto * item = new QStandardItem(Icon("Folder"), QFileInfo(path).fileName());
    item->setEditable(false);
    item->setData(path, PathRole);
    item->setData("folder-path:" + path, KeyRole);
    item->setData(true, FolderRole);
    parent->appendRow(item);
    _Folders.insert(path, item);
    return item;
}

void ProjectAssetsModel::BuildFolders(const std::vector<IAssetMeta *> & metas) {
    clear(); _Folders.clear();
    auto * root = new QStandardItem(Icon("Folder"), "Assets");
    root->setEditable(false); root->setData("root",KeyRole); root->setData("",PathRole); root->setData(true,FolderRole);
    appendRow(root); _Folders.insert("",root);
    std::vector<IAssetMeta *> folders;
    for (IAssetMeta * meta:metas) if (meta->_ImporterType==eAssetImporterType::Folder) folders.push_back(meta);
    std::sort(folders.begin(),folders.end(),[](IAssetMeta * a,IAssetMeta * b){return a->GetPath()<b->GetPath();});
    for (IAssetMeta * meta:folders) {
        auto * item=EnsureFolder(QString::fromStdString(meta->GetPath()));
        const QString guid=QString::fromStdString(meta->_Guid.GetGuidString());
        item->setData(guid,GuidRole);item->setData(guid,KeyRole);
    }
}

QStandardItem * ProjectAssetsModel::SourceItem(IAssetMeta * meta) {
    const QString path=QString::fromStdString(meta->GetPath());
    const QString guid=QString::fromStdString(meta->_Guid.GetGuidString());
    auto * item=new QStandardItem(Icon(ImporterName(meta->_ImporterType)),QFileInfo(path).fileName());
    item->setEditable(false);item->setData(guid,GuidRole);item->setData(guid,KeyRole);item->setData(path,PathRole);
    item->setData(meta->_ImporterType==eAssetImporterType::Folder,FolderRole);item->setData(false,SubAssetRole);
    item->setToolTip(path+"\n"+QString::fromStdString(meta->GetImportError()));
    return item;
}

void ProjectAssetsModel::BuildAssets(const std::vector<IAssetMeta *> & metas) {
    clear();setHorizontalHeaderLabels({"名称","类型","状态"});
    for (IAssetMeta * meta:metas) {
        auto * item=SourceItem(meta);
        auto * type=new QStandardItem(ImporterName(meta->_ImporterType));type->setEditable(false);
        QString status;
        const auto state=meta->GetImportState();
        if (state==eAssetImportState::Pending) status="待导入";
        else if (state==eAssetImportState::Ready) status="就绪";
        else if (state==eAssetImportState::Failed) status="导入失败";
        else if(meta->_ImporterType!=eAssetImporterType::Folder) status="未启用";
        auto * stateItem=new QStandardItem(status);stateItem->setEditable(false);stateItem->setToolTip(QString::fromStdString(meta->GetImportError()));
        if(state==eAssetImportState::Failed) {item->setForeground(ErrorColor);stateItem->setForeground(ErrorColor);}
        else if(state==eAssetImportState::Pending) stateItem->setForeground(PendingColor);
        appendRow({item,type,stateItem});
        if (!meta->GetSubAssets().empty()) {
            auto * placeholder=new QStandardItem();placeholder->setData(true,PlaceholderRole);placeholder->setEditable(false);
            item->appendRow(placeholder);
        }
    }
}

void ProjectAssetsModel::LoadSubAssets(const QModelIndex & index,IAssetsManager * assets) {
    QStandardItem * parent=itemFromIndex(index);
    if (!parent || !parent->hasChildren() || !parent->child(0)->data(PlaceholderRole).toBool()) return;
    const Guid guid(parent->data(GuidRole).toString().toStdString());
    IAssetMeta * meta=assets->QueryAsset(guid);
    parent->removeRows(0,parent->rowCount());
    if (!meta) return;
    const auto children=meta->GetSubAssets();
    for(IAsset * asset:children) {
        const QString id=QString::number(asset->GetLocalID());
        const QString typeName=ResourceName(asset->GetAssetType());
        auto * item=new QStandardItem(Icon(typeName),QString::fromStdString(asset->GetName()));item->setEditable(false);
        item->setData(parent->data(GuidRole),GuidRole);item->setData(id,LocalIDRole);item->setData(true,SubAssetRole);
        item->setData(parent->data(PathRole),PathRole);item->setData(parent->data(GuidRole).toString()+":"+id,KeyRole);
        item->setToolTip(parent->data(PathRole).toString()+"\nLocalID: "+id);
        auto * type=new QStandardItem(typeName);type->setEditable(false);
        auto * status=new QStandardItem("子资源");status->setEditable(false);
        parent->appendRow({item,type,status});
    }
}

QModelIndex ProjectAssetsModel::FindKey(const QString & key,const QModelIndex & parent) const {
    for(int row=0;row<rowCount(parent);++row) {
        const QModelIndex current=index(row,0,parent);
        if(current.data(KeyRole).toString()==key) return current;
        const QModelIndex child=FindKey(key,current);
        if(child.isValid()) return child;
    }
    return {};
}

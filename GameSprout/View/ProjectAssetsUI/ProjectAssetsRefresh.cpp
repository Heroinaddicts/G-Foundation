#include "ProjectAssetsUI.h"
#include "ProjectAssetsModel.h"
#include "ui_ProjectAssetsUI.h"
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QScrollBar>
#include <algorithm>

std::vector<ProjectAssetSelection> ProjectAssetsUI::GetSelection() const {
    std::vector<ProjectAssetSelection> result;
    if (!_Window || !_Ui) return result;
    for (const QModelIndex & index:_Ui->assetTree->selectionModel()->selectedRows(0)) {
        const std::string guid=index.data(ProjectAssetsModel::GuidRole).toString().toStdString();
        if (!Guid(guid).IsValid()) continue;
        result.push_back({guid,index.data(ProjectAssetsModel::LocalIDRole).toString().toULongLong(),index.data(ProjectAssetsModel::SubAssetRole).toBool()});
    }
    if (result.empty() && _FolderSelected && !_CurrentFolderGuid.isEmpty()) result.push_back({_CurrentFolderGuid.toStdString(),0,false});
    return result;
}

bool ProjectAssetsUI::NavigateToDirectory(const std::string & path) {
    if (!_Window || !_Assets) return false;
    QString target=QDir::cleanPath(QString::fromStdString(path));
    if (target==".") target.clear();
    QString guid;
    if (!target.isEmpty()) {
        for (IAssetMeta * meta:_Assets->GetAssetMetas())
            if(meta->_ImporterType==eAssetImporterType::Folder && QString::fromStdString(meta->GetPath())==target) {
                guid=QString::fromStdString(meta->_Guid.GetGuidString());break;
            }
        if(guid.isEmpty()) return false;
    }
    _ResetSelection = target != _CurrentPath;
    _CurrentPath=target;_CurrentFolderGuid=guid;
    _FolderSelected=!guid.isEmpty();
    _Ui->searchEdit->clear();
    _Dirty=true;
    return true;
}

void ProjectAssetsUI::PublishSelection() {
    if (_Selection && !_Rebuilding) _Selection->SetAssetSelection(GetSelection());
}

void ProjectAssetsUI::OnAssetSelectionChanged(const std::vector<ProjectAssetSelection> & selection) {
    if (!_Window || selection == GetSelection()) return;
    _ExternalSelection=selection;
    _RestoreExternalSelection=true;
    if (!selection.empty()) {
        IAssetMeta * meta=_Assets->QueryAsset(Guid(selection.front().GuidString));
        if (meta) {
            const QString parent=QFileInfo(QString::fromStdString(meta->GetPath())).path();
            NavigateToDirectory(parent=="." ? "" : parent.toStdString());
        }
    }
    _FolderSelected=false;
    _Dirty=true;
}

void ProjectAssetsUI::Refresh() {
    if (!_Window || !_Assets) return;
    const auto metas=_Assets->GetAssetMetas();
    QMap<QString,QString> folders;
    folders.insert("","");
    for(IAssetMeta * meta:metas) if(meta->_ImporterType==eAssetImporterType::Folder)
        folders.insert(QString::fromStdString(meta->GetPath()),QString::fromStdString(meta->_Guid.GetGuidString()));
    if (!_CurrentFolderGuid.isEmpty()) {
        IAssetMeta * folder=_Assets->QueryAsset(Guid(_CurrentFolderGuid.toStdString()));
        if(folder && folder->_ImporterType==eAssetImporterType::Folder) _CurrentPath=QString::fromStdString(folder->GetPath());
        else {
            while(!folders.contains(_CurrentPath)) {
                const QString parent=QFileInfo(_CurrentPath).path();
                _CurrentPath=parent=="." ? "" : parent;
            }
            _CurrentFolderGuid=folders.value(_CurrentPath);
            _ResetSelection=true;
        }
    }
    std::vector<QString> selection;
    if (_RestoreExternalSelection) {
        for (const auto & item:_ExternalSelection) {
            const QString guid=QString::fromStdString(item.GuidString);
            selection.push_back(item.IsSubAsset ? guid+":"+QString::number(item.LocalID) : guid);
            if (item.IsSubAsset) _ExpandedAssets.insert(guid);
        }
    } else if(!_ResetSelection) for(const QModelIndex & index:_Ui->assetTree->selectionModel()->selectedRows(0))
        selection.push_back(index.data(ProjectAssetsModel::KeyRole).toString());
    const int folderScroll=_Ui->folderTree->verticalScrollBar()->value();
    const int assetScroll=_ResetSelection ? 0 : _Ui->assetTree->verticalScrollBar()->value();
    _Rebuilding=true;
    struct Guard {bool & Flag;~Guard(){Flag=false;}} guard{_Rebuilding};
    _FolderModel->BuildFolders(metas);
    for(const QString & key:_ExpandedFolders) _Ui->folderTree->expand(_FolderModel->FindKey(key));
    const QString folderKey=_CurrentFolderGuid.isEmpty() ? "root" : _CurrentFolderGuid;
    const QModelIndex folderIndex=_FolderModel->FindKey(folderKey);
    if(folderIndex.isValid()) _Ui->folderTree->setCurrentIndex(folderIndex);
    const QString search=_Ui->searchEdit->text().trimmed();
    std::vector<IAssetMeta *> visible;
    if(search.isEmpty()) visible=_Assets->GetAssetsInDirectory(_CurrentPath.toStdString());
    else for(IAssetMeta * meta:metas) {
        const QString path=QString::fromStdString(meta->GetPath());
        if(!_CurrentPath.isEmpty() && !path.startsWith(_CurrentPath+"/")) continue;
        bool matches=path.contains(search,Qt::CaseInsensitive);
        if(!matches) for(IAsset * child:meta->GetSubAssets())
            if(QString::fromStdString(child->GetName()).contains(search,Qt::CaseInsensitive)) {matches=true;break;}
        if(matches) visible.push_back(meta);
    }
    std::sort(visible.begin(),visible.end(),[](IAssetMeta * left,IAssetMeta * right) {
        const bool a=left->_ImporterType==eAssetImporterType::Folder,b=right->_ImporterType==eAssetImporterType::Folder;
        if(a!=b) return a;
        return QString::compare(QString::fromStdString(left->GetPath()),QString::fromStdString(right->GetPath()),Qt::CaseInsensitive)<0;
    });
    _AssetModel->BuildAssets(visible);
    _Ui->assetTree->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    _Ui->assetTree->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    _Ui->assetTree->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    for(const QString & key:_ExpandedAssets) {
        const QModelIndex index=_AssetModel->FindKey(key);
        if(index.isValid()) { _AssetModel->LoadSubAssets(index,_Assets);_Ui->assetTree->expand(index); }
    }
    for(const QString & key:selection) {
        const QModelIndex index=_AssetModel->FindKey(key);
        if(index.isValid()) _Ui->assetTree->selectionModel()->select(index,QItemSelectionModel::Select|QItemSelectionModel::Rows);
    }
    _Ui->breadcrumbCombo->clear();
    _Ui->breadcrumbCombo->addItem("Assets",QString());
    QString path,display="Assets";
    for(const QString & part:_CurrentPath.split('/',Qt::SkipEmptyParts)) {
        path=path.isEmpty()?part:path+"/"+part;
        display+=" > "+part;
        _Ui->breadcrumbCombo->addItem(display,path);
    }
    _Ui->breadcrumbCombo->setCurrentIndex(_Ui->breadcrumbCombo->count()-1);
    _Ui->upButton->setEnabled(!_CurrentPath.isEmpty());
    _Ui->folderTree->doItemsLayout();_Ui->assetTree->doItemsLayout();
    _Ui->folderTree->verticalScrollBar()->setValue(folderScroll);
    _Ui->assetTree->verticalScrollBar()->setValue(assetScroll);
    _ResetSelection=false;_Dirty=false;_RestoreExternalSelection=false;
    UpdateStatus();
    // The guard still suppresses transient empty selections during model reset.
    if (_Selection) _Selection->SetAssetSelection(GetSelection());
}

void ProjectAssetsUI::UpdateStatus() {
    if(!_Window) return;
    const int count=_AssetModel->rowCount();
    const auto selection=GetSelection();
    QString text=QString("%1 项").arg(count);
    if(!selection.empty()) text+=QString(" · 已选择 %1 项").arg(selection.size());
    _Ui->statusLabel->setText(text);
}

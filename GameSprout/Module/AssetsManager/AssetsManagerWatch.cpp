#include "AssetsManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QUuid>

namespace {
bool MoveSidecar(const QString & oldPath,const QString & newPath) {
    if (oldPath==newPath || !QFileInfo::exists(oldPath)) return true;
    // An intermediate path also works for case-only renames on case-insensitive filesystems.
    const QString temporary=oldPath+".moving-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QFile::rename(oldPath,temporary)) return false;
    if (QFileInfo::exists(newPath) || !QFile::rename(temporary,newPath)) {
        QFile::rename(temporary,oldPath);
        return false;
    }
    return true;
}
}

void AssetsManager::RemoveAssetRecords(const std::string & path,bool recursive) {
    const std::string relative=RelativePath(path), prefix=relative+"/";
    const bool all = recursive && relative == ".";
    std::set<std::string> guids;
    for (const auto & entry : _SourceRecords)
        if (all || entry.second.Path == relative || (recursive && entry.second.Path.compare(0,prefix.size(),prefix) == 0)) guids.insert(entry.first);
    for (const auto & entry : _AssetGuidsByPath)
        if (all || entry.first == relative || (recursive && entry.first.compare(0,prefix.size(),prefix) == 0)) guids.insert(entry.second);
    for (const std::string & guid : guids) {
        const auto found = _Assets.find(guid);
        const auto recorded = _SourceRecords.find(guid);
        const std::string oldPath = found != _Assets.end() ? found->second->GetPath() : recorded->second.Path;
        _Pipeline->Invalidate(Guid(guid));
        ImportDependents(oldPath);
        std::string error;
        if (!ArchiveSourceRecord(guid, "Deleted")) continue;
        _AssetGuidsByPath.erase(oldPath);
        if (found != _Assets.end()) { delete found->second; _Assets.erase(found); }
    }
    const std::string watchedPath=all ? _CanonicalAssetsRoot : path;
    for (auto it=_Watches.begin();it!=_Watches.end();) {
        if (*it==watchedPath || (recursive && it->compare(0,watchedPath.size()+1,watchedPath+"/")==0)) it=_Watches.erase(it);else ++it;
    }
}

void AssetsManager::ReloadMeta(const std::string & sourcePath) {
    const auto byPath=_AssetGuidsByPath.find(RelativePath(sourcePath));
    if (byPath==_AssetGuidsByPath.end()) return;
    IAssetMeta * meta=QueryAsset(Guid(byPath->second));
    if (!meta) return;
    QFile file(QString::fromStdString(sourcePath+".meta"));
    if (!file.exists()) {
        // A removed sidecar is recovered from the live identity rather than issuing another GUID.
        if (!meta->SaveMeta()) GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot restore Meta: {}",sourcePath);
        return;
    }
    Json::Value value;
    if (!file.open(QIODevice::ReadOnly) || !AssetArtifactIO::Parse(file.readAll().toStdString(),value)) {
        _Pipeline->Invalidate(meta->_Guid);
        RecordSourceError(meta->GetPath(), "Invalid Meta");
        GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Invalid Meta: {}",sourcePath);return;
    }
    std::string current;
    if (!meta->SerializeMeta(current)) return;
    Json::Value oldValue;
    AssetArtifactIO::Parse(current,oldValue);
    if (value==oldValue) return; // Includes our own identity/settings saves.
    _Pipeline->Invalidate(meta->_Guid);
    if (!static_cast<AssetMeta *>(meta)->ConvertFromJson(value)) {
        RecordSourceError(meta->GetPath(), "Invalid Meta settings or changed GUID");
        GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Invalid Meta settings or changed GUID: {}",sourcePath);return;
    }
    ImportAsset(meta);ImportDependents(meta->GetPath());
}

void AssetsManager::RenameSource(const Foundation::FileSystemWatcherEvent & event,bool directory) {
    if (event.OldPath.empty() || event.OldPath==event.Path) return;
    const std::string oldRelative=RelativePath(event.OldPath), newRelative=RelativePath(event.Path);
    if (!AssetArtifactIO::RelativePath(oldRelative) || !AssetArtifactIO::RelativePath(newRelative)) return;
    if (!MoveSidecar(QString::fromStdString(event.OldPath+".meta"),QString::fromStdString(event.Path+".meta"))) {
        GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot move Meta from {} to {}",event.OldPath,event.Path);return;
    }
    std::vector<IAssetMeta *> moved;
    std::vector<std::string> oldPaths;
    for (const auto & entry:_Assets) if (entry.second->GetPath()==oldRelative || (directory && entry.second->GetPath().compare(0,oldRelative.size()+1,oldRelative+"/")==0)) moved.push_back(entry.second);
    for (IAssetMeta * meta:moved) {
        const std::string oldPath=meta->GetPath();
        oldPaths.push_back(oldPath);
        const std::string newPath=newRelative+oldPath.substr(oldRelative.size());
        const std::string guid=meta->_Guid.GetGuidString();
        _Pipeline->Invalidate(meta->_Guid);
        _AssetGuidsByPath.erase(oldPath);
        static_cast<AssetMeta *>(meta)->SetPath(newPath);
        _AssetGuidsByPath[newPath]=guid;
        if (!meta->SaveMeta()) GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot update moved Meta: {}",newPath);
        RecordPendingSource(meta);
    }
    for (auto it=_Watches.begin();it!=_Watches.end();) {
        if (*it==event.OldPath || it->compare(0,event.OldPath.size()+1,event.OldPath+"/")==0) it=_Watches.erase(it);else ++it;
    }
    if (directory) { WatcherDir(event.Path);ScanDirectory(event.Path); }
    else {
        const auto found=_AssetGuidsByPath.find(newRelative);
        if (found!=_AssetGuidsByPath.end()) {
            IAssetMeta * meta=QueryAsset(Guid(found->second));
            const std::string extension=QFileInfo(QString::fromStdString(event.Path)).suffix().toStdString();
            const eAssetImporterType importer=GetAssetImporterType(extension);
            if (meta && importer!=eAssetImporterType::Unknown && importer!=meta->_ImporterType) {
                // A real source-category change retains the file GUID but starts with the new defaults.
                std::string json;
                Json::Value value;
                meta->SerializeMeta(json);AssetArtifactIO::Parse(json,value);
                value["ImporterType"]=GetAssetImporterTypeName(importer);
                value["ImportSettings"]=Json::Value(Json::objectValue);
                std::string error;
                if (!AssetArtifactIO::Write(QString::fromStdString(event.Path+".meta"),QByteArray::fromStdString(AssetArtifactIO::JsonText(value)),error)) {
                    GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot change importer: {}",error);return;
                }
                const std::string guid=meta->_Guid.GetGuidString();
                delete meta;_Assets.erase(guid);_AssetGuidsByPath.erase(newRelative);
                const std::string name=QFileInfo(QString::fromStdString(event.Path)).completeBaseName().toStdString();
                StoreAssetMeta(TriggerFactory(importer,event,name,extension,this));
                const auto replacement=_AssetGuidsByPath.find(newRelative);
                if (replacement!=_AssetGuidsByPath.end()) ImportAsset(QueryAsset(Guid(replacement->second)));
            } else if (meta) { ImportAsset(meta);ImportDependents(newRelative); }
        } else OnFileCreate(event,QFileInfo(QString::fromStdString(event.Path)).completeBaseName().toStdString(),QFileInfo(QString::fromStdString(event.Path)).suffix().toStdString(),this);
    }
    // Reverse indexes still use the old paths, but every referenced GUID must
    // already resolve to its new path when the dependent's snapshot is taken.
    for (const std::string & oldPath:oldPaths) ImportDependents(oldPath);
}

void AssetsManager::OnFileCreate(const Foundation::FileSystemWatcherEvent & event,const std::string & name,const std::string & extension,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (!owner || owner!=s_Active) return;
    if (ToLower(extension)==META_EXTENSION) {owner->ReloadMeta(event.Path.substr(0,event.Path.size()-5));return;}
    if (!QFileInfo::exists(QString::fromStdString(event.Path))) return;
    const std::string relative=owner->RelativePath(event.Path);
    auto existing=owner->_AssetGuidsByPath.find(relative);
    if (existing==owner->_AssetGuidsByPath.end()) {
        owner->RestoreMissingMeta(event.Path);
        IAssetMeta * meta = TriggerFactory(GetAssetImporterType(extension), event, name, extension, context);
        if (!meta && GetAssetImporterType(extension) != eAssetImporterType::Unknown) {
            owner->RecordSourceError(relative, "Cannot load source Meta");
            GameSproutConsoleLog(g_IConsole,eConsoleLog::Error,"Cannot load source Meta: {}",event.Path);
        }
        owner->StoreAssetMeta(meta);
        existing=owner->_AssetGuidsByPath.find(relative);
    }
    if (existing!=owner->_AssetGuidsByPath.end()) {
        IAssetMeta * meta=owner->QueryAsset(Guid(existing->second));
        const auto known=owner->_SourceRecords.find(existing->second);
        if (meta && event.Action==Foundation::eFileSystemAction::Create && known!=owner->_SourceRecords.end()) {
            const AssetSourceRecord current=owner->DescribeSource(meta);
            const AssetSourceRecord & previous=known->second;
            if ((previous.Status=="Pending" || previous.Status=="Ready") && previous.SourceSize==current.SourceSize
                && previous.SourceModifyTime==current.SourceModifyTime && previous.MetaModifyTime==current.MetaModifyTime
                && previous.MetaJson==current.MetaJson) return; // Already handled by this directory's synchronous scan.
        }
        owner->ImportAsset(meta);
    }
    owner->ImportDependents(relative);
}
void AssetsManager::OnFileDelete(const Foundation::FileSystemWatcherEvent & event,const std::string &,const std::string & extension,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (!owner || owner!=s_Active) return;
    if (ToLower(extension)==META_EXTENSION) {owner->ReloadMeta(event.Path.substr(0,event.Path.size()-5));return;}
    // Ignore delayed deletes if the same path has already been recreated.
    if (QFileInfo::exists(QString::fromStdString(event.Path))) {OnFileCreate(event,"",extension,context);return;}
    owner->RemoveAssetRecords(event.Path,false);
}
void AssetsManager::OnFileModify(const Foundation::FileSystemWatcherEvent & event,const std::string & name,const std::string & extension,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (!owner || owner!=s_Active) return;
    if (ToLower(extension)==META_EXTENSION) {owner->ReloadMeta(event.Path.substr(0,event.Path.size()-5));return;}
    if (!event.OldPath.empty() && event.OldPath!=event.Path) owner->RenameSource(event,false);
    else OnFileCreate(event,name,extension,context);
}
void AssetsManager::OnFileRename(const Foundation::FileSystemWatcherEvent & event,const std::string &,const std::string & extension,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (!owner || owner!=s_Active) return;
    if (ToLower(extension)==META_EXTENSION) {owner->ReloadMeta(event.Path.substr(0,event.Path.size()-5));return;}
    owner->RenameSource(event,false);
}
void AssetsManager::OnFolderCreate(const Foundation::FileSystemWatcherEvent & event,const std::string & name,const std::string &,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (!owner || owner!=s_Active || !QFileInfo(QString::fromStdString(event.Path)).isDir()) return;
    owner->RestoreMissingMeta(event.Path);
    owner->StoreAssetMeta(TriggerFactory(eAssetImporterType::Folder,event,name,"",context));
    const auto folder=owner->_AssetGuidsByPath.find(owner->RelativePath(event.Path));
    if (folder!=owner->_AssetGuidsByPath.end()) owner->RecordPendingSource(owner->QueryAsset(Guid(folder->second)));
    owner->WatcherDir(event.Path);
    owner->ScanDirectory(event.Path);
}
void AssetsManager::OnFolderDelete(const Foundation::FileSystemWatcherEvent & event,const std::string &,const std::string &,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (!owner || owner!=s_Active || QFileInfo::exists(QString::fromStdString(event.Path))) return;
    owner->RemoveAssetRecords(event.Path,true);
}
void AssetsManager::OnFolderModify(const Foundation::FileSystemWatcherEvent &,const std::string &,const std::string &,void *) {}
void AssetsManager::OnFolderRename(const Foundation::FileSystemWatcherEvent & event,const std::string &,const std::string &,void * context) {
    AssetsManager * owner=static_cast<AssetsManager *>(context);
    if (owner && owner==s_Active) owner->RenameSource(event,true);
}
void AssetsManager::OnFileSystemWatcher(const Foundation::FileSystemWatcherEvent & event,const std::string & name,const std::string & extension,void * context) {
    if (!s_Active || context!=s_Active) return;
    AssetsManager * owner=s_Active;
    auto normalized=[owner](const std::string & raw) {
        QFileInfo info(QString::fromStdString(raw));
        QString path=info.canonicalFilePath();
        if (!path.isEmpty()) return path;
        const QString parent=info.dir().canonicalPath();
        if (!parent.isEmpty()) return QDir(parent).filePath(info.fileName());
        path=QDir::cleanPath(info.absoluteFilePath());
        const QString lexical=QDir(QString::fromStdString(owner->_AssetsRoot)).absolutePath();
        if (path==lexical || path.startsWith(lexical+"/")) path=QString::fromStdString(owner->_CanonicalAssetsRoot)+path.mid(lexical.size());
        return path;
    };
    const QString root=QString::fromStdString(owner->_CanonicalAssetsRoot);
    const QString path=normalized(event.Path);
    const QString old=event.OldPath.empty()?QString():normalized(event.OldPath);
    if (event.Type==Foundation::eWatcherType::Directory && (path==root || old==root)) {
        if (event.Action==Foundation::eFileSystemAction::Delete || (event.Action==Foundation::eFileSystemAction::Rename && old==root && path!=root)) {
            owner->RemoveAssetRecords(owner->_AssetsRoot,true);
        } else if (path==root && (event.Action==Foundation::eFileSystemAction::Create || event.Action==Foundation::eFileSystemAction::Rename)) {
            owner->WatcherDir(owner->_AssetsRoot);
            owner->ScanDirectory(owner->_AssetsRoot);
        }
        return;
    }
    // The project-root watch is only for Assets itself, never Library/Info/other directories.
    if (!path.startsWith(root+"/")) {
        if (old.startsWith(root+"/")) owner->RemoveAssetRecords(event.OldPath,event.Type==Foundation::eWatcherType::Directory);
        return;
    }
    if (!old.isEmpty() && !old.startsWith(root+"/")) {
        if (event.Type==Foundation::eWatcherType::Directory) OnFolderCreate(event,name,extension,context);
        else OnFileCreate(event,name,extension,context);
        return;
    }
    if (event.Type==Foundation::eWatcherType::File) {
        switch (event.Action) {
        case Foundation::eFileSystemAction::Create:OnFileCreate(event,name,extension,context);break;
        case Foundation::eFileSystemAction::Modify:OnFileModify(event,name,extension,context);break;
        case Foundation::eFileSystemAction::Delete:OnFileDelete(event,name,extension,context);break;
        case Foundation::eFileSystemAction::Rename:OnFileRename(event,name,extension,context);break;
        }
    } else {
        switch (event.Action) {
        case Foundation::eFileSystemAction::Create:OnFolderCreate(event,name,extension,context);break;
        case Foundation::eFileSystemAction::Modify:OnFolderModify(event,name,extension,context);break;
        case Foundation::eFileSystemAction::Delete:OnFolderDelete(event,name,extension,context);break;
        case Foundation::eFileSystemAction::Rename:OnFolderRename(event,name,extension,context);break;
        }
    }
}

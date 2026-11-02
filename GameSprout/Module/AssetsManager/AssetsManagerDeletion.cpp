#include "AssetsManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QThread>

bool AssetsManager::DeleteSourceAsset(const Guid& guid,const std::string& expectedPath,std::string& error) {
    error.clear();
    if(!QCoreApplication::instance()||QThread::currentThread()!=QCoreApplication::instance()->thread()||
       !_SourceDatabaseReady||_StartupLoading||_DispatchingEvents||!_Pipeline||!_Library) {
        error="资源服务正在初始化、派发事件或不在所属主线程，暂时不能删除。";return false;
    }
    auto* meta=QueryAsset(guid);
    if(!meta||meta->GetPath()!=expectedPath||_ConflictingGuids.contains(guid.GetGuidString())||
       !AssetArtifactIO::RelativePath(expectedPath)) {
        error="资源已移动、删除或身份冲突，请重新选择后再删除。";return false;
    }
    const QString root=QFileInfo(QString::fromStdString(_CanonicalAssetsRoot)).canonicalFilePath();
    const QFileInfo source(QDir(root).filePath(QString::fromStdString(expectedPath)));
    const QString path=source.absoluteFilePath(),canonical=source.canonicalFilePath();
    if(root.isEmpty()||canonical.isEmpty()||canonical==root||!canonical.startsWith(root+'/')||source.isSymLink()||
       (!source.isFile()&&!source.isDir())||canonical!=QDir::cleanPath(path)) {
        error="只能删除 Assets 内的普通源文件或目录，不能删除根目录或符号链接。";return false;
    }
    const bool directory=source.isDir();
    if(directory) {
        QDirIterator entries(path,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
        while(entries.hasNext()) {
            entries.next();const auto entry=entries.fileInfo();
            if(entry.isSymLink()||(!entry.isFile()&&!entry.isDir())||!entry.canonicalFilePath().startsWith(root+'/')) {
                error="目录包含符号链接或不支持的条目，未执行删除。";return false;
            }
        }
    }
    const QFileInfo sidecar(path+".meta");
    if(sidecar.isSymLink()||(sidecar.exists()&&!sidecar.isFile())) {
        error="资源 Meta 不是普通文件，未执行删除。";return false;
    }
    if(sidecar.exists()) {
        QFile file(sidecar.absoluteFilePath());Json::Value value;std::string validation;
        if(!file.open(QIODevice::ReadOnly)||file.size()>16*1024*1024||
           !AssetArtifactIO::Parse(file.readAll().toStdString(),value)||!AssetMetaFormat::Validate(value,validation)||
           value["Guid"]!=guid.GetGuidString()||value["Path"]!=expectedPath) {
            error="磁盘 Meta 已变化或无效，未删除其他身份的资源。请等待资源刷新后重试。";return false;
        }
    }
    // No event-loop pumping: a watcher cannot restore the sidecar in between
    // moving the source and moving its identity into the trash.
    QString trashedSource,trashedMeta;
    if(!QFile::moveToTrash(path,&trashedSource)) {
        error="无法将资源移入系统废纸篓，未修改资源数据库。";return false;
    }
    if(sidecar.exists()&&!QFile::moveToTrash(sidecar.absoluteFilePath(),&trashedMeta)) {
        const bool restored=!trashedSource.isEmpty()&&QDir().rename(trashedSource,path);
        if(restored) {error="无法将 Meta 移入废纸篓，已恢复源资源。";return false;}
        // The source has gone; still invalidate it so cached resources cannot
        // masquerade as present. The durable backup retains the Meta contents.
        RemoveAssetRecords(path.toStdString(),directory);
        error="源资源已移入废纸篓，但 Meta 移动和源恢复失败；请检查废纸篓。";return false;
    }
    if(!RemoveAssetRecords(path.toStdString(),directory)) {
        error="源资源和 Meta 已移入废纸篓，但资源数据库更新失败；请检查 Console 并恢复资源后重试。";return false;
    }
    return true;
}

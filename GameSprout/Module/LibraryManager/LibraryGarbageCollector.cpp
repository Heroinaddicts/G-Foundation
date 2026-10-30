#include "LibraryManager.h"
#include "../Shared/AssetArtifactIO.h"
#include <set>
#include <stdexcept>
#include <QUuid>

void LibraryManager::StartGarbageCollection() {
    if (!_Tasks || !_IDE || !_LDBSession || _TaskOwner==Foundation::InvalidTaskOwnerId || _CleanupTasks!=0) return;
    const std::string root=_LibraryRoot;
    const QDateTime startup=_StartupTime;
    Foundation::TaskSubmitOptions options;options._OwnerId=_TaskOwner;
    _IDE->Loading("清理资源缓存","检查遗留产物");++_CleanupTasks;
    bool transferred=false;
    try {
        const auto submitted=_Tasks->SubmitTask(options,
            [root,startup](Int64 & code,Foundation::ITaskContext & context) {
                auto * candidates=new std::vector<std::string>();
                if(!context.SetOwned(candidates)) {delete candidates;return false;}
                const QDir library(QString::fromStdString(root));
                for (const QFileInfo & file : library.entryInfoList({"*.gsasset"},QDir::Files|QDir::NoSymLinks)) {
                    const QString stem=file.completeBaseName();
                    const int separator=stem.indexOf('_');
                    bool validID=false; const auto id=stem.mid(separator+1).toULongLong(&validID);
                    if(separator==32 && Guid(stem.left(separator).toStdString()).IsValid() && validID && id!=0)
                        candidates->push_back(file.fileName().toStdString());
                }
                const QDir artifacts(library.filePath("Artifacts"));
                for(const QFileInfo & directory:artifacts.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
                    if(context.IsCancellationRequested()) return false;
                    if(!Guid(directory.fileName().toStdString()).IsValid()) continue;
                    const QDir versions(directory.absoluteFilePath());
                    for(const QFileInfo & version:versions.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks))
                        candidates->push_back(library.relativeFilePath(version.absoluteFilePath()).toStdString());
                }
                const QDir staging(library.filePath("Staging"));
                for(const QFileInfo & directory:staging.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
                    if(directory.lastModified()<startup) candidates->push_back(library.relativeFilePath(directory.absoluteFilePath()).toStdString());
                }
                code=0;return true;
            },
            [this,root](const Foundation::TaskCompletion &,bool success,const Foundation::ITaskContext & context) {
                struct Balance {LibraryManager * Owner;~Balance(){--Owner->_CleanupTasks;Owner->_IDE->Unloading();}} balance{this};
                const auto * candidates=context.Get<std::vector<std::string>>();
                if(!success || !candidates) return;
                std::set<std::string> referenced;
                const bool valid=_LDBSession->GetTable("Manifests")->ForEach([&](const std::string &,const void * bytes,int length) {
                    Json::Value value;AssetImportManifest manifest;
                    if(!AssetArtifactIO::Parse(std::string(static_cast<const char *>(bytes),size_t(length)),value)
                        || !AssetArtifactIO::FromJson(value,manifest)) throw std::runtime_error("Invalid manifest during cache cleanup");
                    if(!manifest.Directory.empty()) referenced.insert(manifest.Directory);
                    for(const AssetArtifact & artifact:manifest.Assets)
                        referenced.insert(manifest.Directory.empty()?artifact.Path:manifest.Directory+"/"+artifact.Path);
                    return true;
                });
                if(!valid) return;
                std::vector<std::string> garbage;
                const QDir library(QString::fromStdString(root));
                const QString trash="Staging/trash-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                for(const std::string & candidate:*candidates) if(!referenced.count(candidate)) {
                    if(candidate.find('/')==std::string::npos) {
                        // Move orphan files on the main thread, before another import can
                        // reuse their stable names. Workers only delete this private trash.
                        if(!library.mkpath(trash)) continue;
                        const QString name=QString::fromStdString(candidate);
                        if(QFile::rename(library.filePath(name),library.filePath(trash+"/"+name)))
                            garbage.push_back((trash+"/"+name).toStdString());
                    } else garbage.push_back(candidate);
                }
                if(QDir(library.filePath(trash)).exists()) garbage.push_back(trash.toStdString());
                if(garbage.empty()) return;
                Foundation::TaskSubmitOptions options;options._OwnerId=_TaskOwner;
                _IDE->Loading("清理资源缓存","删除过期产物");++_CleanupTasks;
                bool transferred=false;
                try {
                    const auto deletion=_Tasks->SubmitTask(options,
                        [root,garbage](Int64 & code,Foundation::ITaskContext & task) {
                            const QDir library(QString::fromStdString(root));
                            const QString canonical=library.canonicalPath();
                            bool success=true;
                            for(const std::string & path:garbage) {
                                if(task.IsCancellationRequested()) return false;
                                const QFileInfo item(library.filePath(QString::fromStdString(path)));
                                if(!item.exists()) continue;
                                if(item.isSymLink() || !item.canonicalFilePath().startsWith(canonical+"/")) {success=false;continue;}
                                if(item.isDir()? !QDir(item.absoluteFilePath()).removeRecursively() : !QFile::remove(item.absoluteFilePath())) success=false;
                            }
                            QDir legacy(library.filePath("Artifacts"));
                            for(const QFileInfo & directory:legacy.entryInfoList(QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks)) {
                                if(Guid(directory.fileName().toStdString()).IsValid() && QDir(directory.absoluteFilePath()).isEmpty())
                                    legacy.rmdir(directory.fileName());
                            }
                            if(legacy.exists() && legacy.isEmpty()) library.rmdir("Artifacts");
                            code=success?0:-1;return success;
                        },
                        [this](const Foundation::TaskCompletion &,bool success,const Foundation::ITaskContext &) {
                            --_CleanupTasks;_IDE->Unloading();
                            if(!success) GameSproutConsoleLog(g_IConsole,eConsoleLog::Warning,"Some obsolete artifact directories could not be removed");
                        });
                    transferred=deletion.Accepted();
                    if(!transferred) {--_CleanupTasks;_IDE->Unloading();transferred=true;}
                } catch(...) {if(!transferred){--_CleanupTasks;_IDE->Unloading();}throw;}
            });
        transferred=submitted.Accepted();
        if(!transferred){--_CleanupTasks;_IDE->Unloading();transferred=true;}
    } catch(...) {if(!transferred){--_CleanupTasks;_IDE->Unloading();}throw;}
}

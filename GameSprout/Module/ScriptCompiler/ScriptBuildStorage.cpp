#include "ScriptBuildStorage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace ScriptBuildStorage {
namespace {
bool Files(const QString& root,QStringList& files,QStringList& directories,std::string& error) {
    QDirIterator entries(root,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(entries.hasNext()) {
        entries.next();const QFileInfo info=entries.fileInfo();
        if(info.isSymLink()) {error="Symbolic links are not supported in script Sources: "+info.filePath().toStdString();return false;}
        if(info.isDir()) directories.push_back(info.filePath());
        else if(info.isFile()) files.push_back(info.filePath());
        else {error="Unsupported cache entry in script Sources: "+info.filePath().toStdString();return false;}
    }
    return true;
}
}
bool WriteIfChanged(const QString& path,const QByteArray& bytes,std::string& error) {
    if(QFileInfo(path).isSymLink()) {error="Cannot replace a symbolic link in script cache: "+path.toStdString();return false;}
    QFile previous(path);
    if(previous.open(QIODevice::ReadOnly)) {
        const QByteArray existing=previous.readAll();
        if(previous.error()==QFileDevice::NoError && existing==bytes) return true;
    }
    previous.close();
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        error="Cannot write "+path.toStdString()+": "+file.errorString().toStdString();return false;
    }
    return true;
}
bool Synchronize(const QString& root,const std::vector<SourceSnapshot>& sources,std::string& error) {
    if(QFileInfo(root).isSymLink() || !QDir().mkpath(root)) {error="Cannot create fixed script Sources directory";return false;}
    std::set<QString> wanted={"CMakeLists.txt","GameLogic.generated.cpp","GameLogic.reflection.cpp","GameLogic.buildstamp.cpp"};
    for(const auto& source:sources) wanted.insert(source.Relative);
    QStringList files,directories;
    if(!Files(root,files,directories,error)) return false;
    for(const QString& file:files) if(!wanted.contains(QDir(root).relativeFilePath(file))) {
        if(!QFile::remove(file)) {error="Cannot remove deleted script snapshot: "+file.toStdString();return false;}
    }
    std::sort(directories.begin(),directories.end(),[](const QString& a,const QString& b){return a.size()>b.size();});
    for(const QString& directory:directories) {
        if(!QDir().rmdir(directory) && QDir(directory).isEmpty()) {error="Cannot remove empty script snapshot directory: "+directory.toStdString();return false;}
    }
    for(const auto& source:sources) {
        const QString path=QDir(root).filePath(source.Relative);
        if(!QDir().mkpath(QFileInfo(path).absolutePath()) || !WriteIfChanged(path,source.Bytes,error)) return false;
    }
    return true;
}
QString FileHash(const QString& path,std::string& error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {error="Cannot read script build file: "+path.toStdString();return {};}
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while(!file.atEnd()) {
        const QByteArray bytes=file.read(1024*1024);
        if(file.error()!=QFileDevice::NoError) {error="Cannot hash script build file: "+path.toStdString();return {};}
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject ReadRecord(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document=QJsonDocument::fromJson(file.readAll());
    return document.isObject()?document.object():QJsonObject{};
}
bool WriteRecord(const QString& path,const QJsonObject& record,std::string& error) {
    return WriteIfChanged(path,QJsonDocument(record).toJson(QJsonDocument::Compact),error);
}
QJsonObject SnapshotHashes(const QString& root,std::string& error) {
    QStringList files,directories;
    if(!Files(root,files,directories,error)) return {};
    QJsonObject result;
    for(const QString& file:files) {
        const QString hash=FileHash(file,error);
        if(hash.isEmpty()) return {};
        result.insert(QDir(root).relativeFilePath(file),hash);
    }
    return result;
}
bool SnapshotMatches(const QString& root,const QJsonObject& hashes) {
    if(hashes.isEmpty()) return false;
    std::string error;
    const QJsonObject actual=SnapshotHashes(root,error);
    return error.empty() && actual==hashes;
}
bool ResetCMakeState(const QString& build,std::string& error) {
    const QDir directory(build);
    // Only fixed Build's generated CMake state/objects are invalidated. Source
    // snapshots, candidate Output and the last successful record stay in place.
    const QStringList controls={"CMakeCache.txt","CMakeFiles","Makefile","cmake_install.cmake",".ninja_deps",".ninja_log","build.ninja"};
    for(const QFileInfo& entry:directory.entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot)) {
        const QString name=entry.fileName();
        const bool generated=controls.contains(name) || name.endsWith(".ninja") || name.endsWith(".xcodeproj") || name.endsWith(".sln") || name.endsWith(".vcxproj") || name.endsWith(".vcxproj.filters") || name.endsWith(".dir") || name.endsWith(".build");
        if(!generated) continue;
        const bool removed=entry.isSymLink()?QFile::remove(entry.filePath()):entry.isDir()?QDir(entry.filePath()).removeRecursively():QFile::remove(entry.filePath());
        if(!removed) {error="Cannot reset CMake state: "+entry.filePath().toStdString();return false;}
    }
    return true;
}
}

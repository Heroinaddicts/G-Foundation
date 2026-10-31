#include "ModelTextureResolver.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace {
bool Inside(const QString& path,const QString& root) {
    return path==root||path.startsWith(root+'/');
}
bool Absolute(const QString& path) {
    // Unix的QDir不会把C:/...识别为绝对路径，仍必须将其当作者机器的绝对引用。
    return QDir::isAbsolutePath(path)||(path.size()>=3&&path[0].isLetter()&&path[1]==':'&&path[2]=='/');
}
struct Candidate {QString Directory,Name;};
}

std::string NormalizeModelTexturePath(const std::string& path) {
    QString normalized=QString::fromStdString(path);normalized.replace('\\','/');return normalized.toStdString();
}
ModelTextureResolution ResolveModelTexture(const std::string& assetsRoot,const std::string& sourcePath,
    const std::string& filename,const std::string& relativeFilename,const std::vector<std::string>& materialDirectories,
    const std::function<void(const std::string&)>& beforeSearchDirectory) {
    const QString root=QFileInfo(QString::fromStdString(assetsRoot)).canonicalFilePath();
    if(root.isEmpty()||!QFileInfo(root).isDir())return {{},"Assets directory is unavailable"};
    const QDir assets(root);
    const QString source=QString::fromStdString(NormalizeModelTexturePath(sourcePath));
    const QFileInfo model(assets.filePath(source));
    const QString modelDirectory=QFileInfo(model.absolutePath()).canonicalFilePath();
    if(modelDirectory.isEmpty()||!Inside(modelDirectory,root))return {{},"Model directory is outside Assets or unavailable"};
    const QString absoluteName=QString::fromStdString(NormalizeModelTexturePath(filename));
    const QString relativeName=QString::fromStdString(NormalizeModelTexturePath(relativeFilename));
    QString basename=QFileInfo(absoluteName).fileName();
    if(basename.isEmpty())basename=QFileInfo(relativeName).fileName();
    if(basename.isEmpty()||basename=="."||basename==".."||basename.contains(QChar(0)))return {{},"Texture reference has no valid filename"};
    std::vector<Candidate> candidates;
    const auto add=[&](const QString& directory,const QString& name) {
        if(!name.isEmpty())candidates.push_back({QDir::cleanPath(directory),name});
    };
    add(modelDirectory,basename);
    add(QDir(modelDirectory).filePath(model.completeBaseName()+".fbm"),basename);
    for(const auto& directory:materialDirectories)add(assets.filePath(QString::fromStdString(NormalizeModelTexturePath(directory))),basename);
    QString ancestor=modelDirectory;
    while(Inside(ancestor,root)) {
        add(QDir(ancestor).filePath("Textures"),basename);
#if !defined(_WIN32)
        // 在大小写敏感的平台也支持常见的textures目录拼法。
        add(QDir(ancestor).filePath("textures"),basename);
#endif
        if(ancestor==root)break;
        const QString parent=QFileInfo(ancestor).absolutePath();if(parent==ancestor)break;ancestor=parent;
    }
    // 精确的源相对引用最后尝试，OBJ相对MTL目录的路径也按已有依赖枚举顺序处理。
    if(!relativeName.isEmpty()&&!Absolute(relativeName)) {
        for(const auto& directory:materialDirectories) {
            const QFileInfo path(QDir(assets.filePath(QString::fromStdString(NormalizeModelTexturePath(directory)))).filePath(relativeName));
            add(path.absolutePath(),path.fileName());
        }
        const QFileInfo path(QDir(modelDirectory).filePath(relativeName));add(path.absolutePath(),path.fileName());
    }
    for(const auto& original:{relativeName,absoluteName})if(!original.isEmpty()&&Absolute(original)) {
        const QFileInfo path(original);add(path.absolutePath(),path.fileName());
    }
    bool outside=false;QSet<QString> searched;
    for(const auto& candidate:candidates) {
        // 缺失目录也记录，后续新增 Textures/.fbm 必须使快速校验失效。
        const QString lexical=QDir::cleanPath(QFileInfo(candidate.Directory).absoluteFilePath());
        if(beforeSearchDirectory && Inside(lexical,root)) beforeSearchDirectory(lexical.toStdString());
        const QString directory=QFileInfo(candidate.Directory).canonicalFilePath();
        if(directory.isEmpty())continue;
        if(!Inside(directory,root)){outside=true;continue;}
        const QString identity=directory+'\n'+candidate.Name.toCaseFolded();
        if(searched.contains(identity))continue;searched.insert(identity);
        QStringList matches;
        const auto entries=QDir(directory).entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDir::NoSort);
        for(const auto& entry:entries) {
            if(entry.fileName().compare(candidate.Name,Qt::CaseInsensitive)!=0)continue;
            const QString canonical=entry.canonicalFilePath();
            // 同名目录/非普通文件不能伪装成missing；交给调用方的严格依赖检查报告错误。
            if(canonical.isEmpty())continue;
            if(!Inside(canonical,root)){outside=true;continue;}
            if(!matches.contains(canonical))matches.push_back(canonical);
        }
        if(matches.size()>1) {
            std::sort(matches.begin(),matches.end());
            return {{},"Ambiguous texture filename '"+candidate.Name.toStdString()+"' in "+assets.relativeFilePath(directory).toStdString()};
        }
        if(matches.size()==1)return {assets.relativeFilePath(matches.front()).toStdString(),{}};
    }
    return {{},outside?"Referenced texture is outside Assets or missing from local search directories":"Referenced texture is missing from local search directories"};
}

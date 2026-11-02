#include "ScriptBuildStorage.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>
#include <map>

namespace ScriptBuildStorage {
namespace {
QString AbsolutePath(const QString& root,const QString& path) {
    return QDir::cleanPath(QDir::isAbsolutePath(path)?path:QDir(root).absoluteFilePath(path));
}
bool IsObject(const QString& path) {
    return path.endsWith(".o") || path.endsWith(".obj");
}
QStringList MakeWords(const QString& text) {
    QStringList result;
    QString word;
    for(qsizetype i=0;i<text.size();++i) {
        const QChar character=text[i];
        if(character=='\\' && i+1<text.size()) word+=text[++i];
        else if(character=='$' && i+1<text.size() && text[i+1]=='$') {word+='$';++i;}
        else if(character.isSpace()) {
            if(!word.isEmpty()) {result.push_back(word);word.clear();}
        } else if(character=='#') break;
        else word+=character;
    }
    if(!word.isEmpty()) result.push_back(word);
    return result;
}
qsizetype RuleSeparator(const QString& line) {
    qsizetype wordStart=0;
    for(qsizetype i=0;i<line.size();++i) {
        if(line[i]=='\\') {++i;continue;}
        if(line[i].isSpace()) {wordStart=i+1;continue;}
        // A Windows drive prefix is part of a path, not a Make rule separator.
        if(line[i]==':' && !(i==wordStart+1 && line[i-1].isLetter() && i+1<line.size() &&
                            (line[i+1]=='/' || line[i+1]=='\\'))) return i;
    }
    return -1;
}
using Dependencies=std::map<QString,std::set<QString>>;
void ReadMakeDependencies(const QString& build,QString text,Dependencies& dependencies) {
    text.replace("\\\r\n"," ");text.replace("\\\n"," ");
    for(const QString& line:text.split('\n')) {
        const qsizetype separator=RuleSeparator(line);
        if(separator<0) continue;
        const QStringList targets=MakeWords(line.left(separator));
        const QStringList inputs=MakeWords(line.mid(separator+1));
        for(const QString& target:targets) if(IsObject(target)) {
            auto& paths=dependencies[AbsolutePath(build,target)];
            for(const QString& input:inputs) paths.insert(AbsolutePath(build,input));
        }
    }
}
void ReadCMakeDependencies(const QString& build,const QString& text,Dependencies& dependencies) {
    QString target;
    for(QString line:text.split('\n')) {
        if(line.endsWith('\r')) line.chop(1);
        if(line.isEmpty() || line.startsWith('#')) continue;
        if(!line.front().isSpace()) {
            target=IsObject(line)?AbsolutePath(build,line):QString();
        } else if(!target.isEmpty()) {
            // CMake's internal format stores an entire unescaped path per line.
            dependencies[target].insert(AbsolutePath(build,line.mid(1)));
        }
    }
}
bool RemoveManagedObject(const QString& build,const QString& path,std::string& error) {
    const QString root=QDir::cleanPath(QFileInfo(build).absoluteFilePath())+'/';
    if(!path.startsWith(root)) return true;
    const QFileInfo object(path);
    if(!object.exists() && !object.isSymLink()) return true;
    const QString canonicalRoot=QFileInfo(build).canonicalFilePath()+'/';
    if(object.isSymLink() || !object.isFile() || !object.canonicalFilePath().startsWith(canonicalRoot)) {
        error="Unsafe script object dependency path: "+path.toStdString();return false;
    }
    if(!QFile::remove(path)) {error="Cannot invalidate changed script object: "+path.toStdString();return false;}
    return true;
}
}

bool PrepareIncrementalBuild(const QString& build,const QJsonObject& inputs,std::string& error) {
    const QString recordPath=QDir(build).filePath("ScriptDependencyInputs.json");
    const QJsonObject record=ReadRecord(recordPath);
    const bool known=record.value("Version").toInt()==1 && record.value("Inputs").isObject();
    const QJsonObject previous=record.value("Inputs").toObject();
    std::set<QString> changed;
    for(auto i=previous.begin();i!=previous.end();++i) if(inputs.value(i.key())!=i.value()) changed.insert(i.key());
    for(auto i=inputs.begin();i!=inputs.end();++i) if(previous.value(i.key())!=i.value()) changed.insert(i.key());
    if(known && changed.empty()) return true;
    Dependencies dependencies;
    QDirIterator entries(build,QStringList{"*.o.d","*.obj.d","compiler_depend.internal"},
                         QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(entries.hasNext()) {
        const QString path=entries.next();QFile file(path);
        if(!file.open(QIODevice::ReadOnly)) {error="Cannot read script dependencies: "+path.toStdString();return false;}
        const QByteArray bytes=file.readAll();
        if(file.error()!=QFileDevice::NoError) {error="Cannot read script dependencies: "+path.toStdString();return false;}
        if(QFileInfo(path).fileName()=="compiler_depend.internal") ReadCMakeDependencies(build,QString::fromUtf8(bytes),dependencies);
        else ReadMakeDependencies(build,QString::fromUtf8(bytes),dependencies);
    }
    for(const auto& [object,paths]:dependencies) {
        const bool affected=!known || std::any_of(paths.begin(),paths.end(),[&](const QString& path){return changed.contains(path);});
        if(affected && !RemoveManagedObject(build,object,error)) return false;
    }
    // Record attempted inputs before invoking the compiler. A failed compilation
    // may have produced some objects; reverting a source must invalidate those too.
    return WriteRecord(recordPath,{{"Version",1},{"Inputs",inputs}},error);
}
}

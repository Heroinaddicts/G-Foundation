#include "ScriptBuildStorage.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
void Write(const QString& path,const QByteArray& bytes) {
    Require(QDir().mkpath(QFileInfo(path).absolutePath()),"Cannot create fixture directory");
    std::string error;
    Require(ScriptBuildStorage::WriteIfChanged(path,bytes,error),error);
}
QString Hash(const QString& path) {
    std::string error;const QString hash=ScriptBuildStorage::FileHash(path,error);
    Require(!hash.isEmpty(),error);return hash;
}
QString MakePath(QString path) {
    path.replace('\\',"\\\\");path.replace('$',"$$");path.replace('#',"\\#");path.replace(' ',"\\ ");return path;
}
void Stamp(const QString& path,const QDateTime& timestamp) {
    QFile file(path);Require(file.open(QIODevice::ReadWrite),"Cannot open fixture timestamp");
    Require(file.setFileTime(timestamp,QFileDevice::FileModificationTime),"Cannot set fixture timestamp");
}
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    try {
        QTemporaryDir temporary;Require(temporary.isValid(),"Cannot create isolated dependency fixture");
        const QString build=temporary.filePath("Build");
        const QString header=temporary.filePath("Source Tree/Target #$ name.hpp");
        const QString sdk=temporary.filePath("SDK/Internal/Runtime.h");
        const QString helper=temporary.filePath("Source Tree/Independent.cpp");
        const QString aggregate=QDir(build).filePath("CMakeFiles/GameLogic.dir/Aggregate name.cpp.o");
        const QString independent=QDir(build).filePath("CMakeFiles/GameLogic.dir/Independent.cpp.o");
        const QString internal=QDir(build).filePath("CMakeFiles/GameLogic.dir/Internal.cpp.o");
        const QString outside=temporary.filePath("outside.o");
        Write(header,"version1");Write(sdk,"sdk1");Write(helper,"independent");Write(outside,"outside");
        Require(QDir().mkpath(build),"Cannot create Build");
        QJsonObject inputs{{header,Hash(header)},{sdk,Hash(sdk)},{helper,Hash(helper)}};
        std::string error;
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error),error);
        Write(aggregate,"aggregate");Write(independent,"independent");Write(internal,"internal");
        Write(aggregate+".d",(MakePath(QDir(build).relativeFilePath(aggregate))+": \\\r\n  "+MakePath(header)+" \\\n  "+MakePath(sdk)+"\n").toUtf8());
        // No whitespace after ':' is valid Make syntax, even before '/'.
        Write(independent+".d",(MakePath(QDir(build).relativeFilePath(independent))+":"+MakePath(helper)+"\n").toUtf8());
        Write(QDir(build).filePath("CMakeFiles/GameLogic.dir/compiler_depend.internal"),
              ("# CMake dependencies\n"+QDir(build).relativeFilePath(internal)+"\n "+header+"\n "+sdk+"\n").toUtf8());
        Write(QDir(build).filePath("external.o.d"),(MakePath(outside)+": "+MakePath(header)+"\n").toUtf8());

        const QDateTime sameSecond=QDateTime::fromMSecsSinceEpoch((QDateTime::currentMSecsSinceEpoch()/1000)*1000);
        Stamp(header,sameSecond);Stamp(aggregate,sameSecond);Stamp(internal,sameSecond);
        Write(header,"version2");Stamp(header,sameSecond);
        Require(QFileInfo(header).lastModified()==QFileInfo(aggregate).lastModified(),"Fixture must have identical source/object timestamps");
        const QJsonObject original=inputs;
        inputs.insert(header,Hash(header));
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error),error);
        Require(!QFileInfo::exists(aggregate) && !QFileInfo::exists(internal),"Changed same-second header must invalidate both dependency formats");
        Require(QFileInfo::exists(independent) && QFileInfo::exists(outside),"Unrelated helper and outside object must remain intact");

        Write(aggregate,"version2 object");Write(internal,"version2 object");
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error),error);
        Require(QFileInfo::exists(aggregate) && QFileInfo::exists(internal),"No input change must retain existing objects");
        // These objects may come from a partly failed compilation. Reverting to
        // the previous successful input still has to invalidate their new code.
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,original,error),error);
        Require(!QFileInfo::exists(aggregate) && !QFileInfo::exists(internal),"Reverting attempted inputs must invalidate partial objects");

        Write(aggregate,"sdk consumer");Write(internal,"sdk consumer");
        inputs=original;inputs.insert(sdk,"different SDK content hash");
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error),error);
        Require(!QFileInfo::exists(aggregate) && !QFileInfo::exists(internal) && QFileInfo::exists(independent),"SDK change must invalidate only consumers");
        inputs.insert(helper,"changed independent input");
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error),error);
        Require(!QFileInfo::exists(independent),"A rule without separator whitespace must still invalidate its object");
        Write(aggregate,"deleted header consumer");inputs.remove(header);
        Require(ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error),error);
        Require(!QFileInfo::exists(aggregate),"Deleted input must invalidate dependent objects");

#if defined(Q_OS_UNIX)
        const QString linkedDirectory=QDir(build).filePath("linked");
        Require(QFile::link(temporary.path(),linkedDirectory),"Cannot make isolated symlink fixture");
        Write(QDir(build).filePath("unsafe.o.d"),(MakePath("linked/outside.o")+": "+MakePath(helper)+"\n").toUtf8());
        inputs.insert(helper,"changed helper");error.clear();
        Require(!ScriptBuildStorage::PrepareIncrementalBuild(build,inputs,error) && error.find("Unsafe script object")!=std::string::npos,
                "An ancestor symlink must never allow deleting an outside object");
        Require(QFileInfo::exists(outside),"Outside object must survive unsafe dependency metadata");
        QFile::remove(linkedDirectory);
#endif
        std::cout<<"Content-based dependency invalidation passed\n";return 0;
    } catch(const std::exception& exception) {
        std::cerr<<exception.what()<<'\n';return 1;
    }
}

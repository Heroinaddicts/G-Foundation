#include "ScriptCompiler.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <algorithm>
#include <csignal>
#include <exception>
#include <set>

namespace {
volatile std::sig_atomic_t interrupted=0;
void Interrupt(int signal) {interrupted=signal;}
bool Cancelled() {return interrupted!=0;}
bool NativeSource(const QString& extension) {
    return extension=="hpp"||extension=="h"||extension=="hxx"||extension=="inl"||
           extension=="cpp"||extension=="cc"||extension=="cxx";
}
bool Guid(const QString& text) {
    if(text.size()!=32) return false;
    bool nonzero=false;
    for(const auto character:text) {
        if(!((character>='0'&&character<='9')||(character>='a'&&character<='f'))) return false;
        nonzero=nonzero||character!='0';
    }
    return nonzero;
}
bool ReadObject(const QString& path,QJsonObject& object,QString& error) {
    const QFileInfo info(path);
    if(!info.isFile()||info.isSymLink()) {error="Missing or unsupported metadata file: "+path;return false;}
    if(info.size()>1024*1024) {error="Metadata file exceeds 1 MiB: "+path;return false;}
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {error="Cannot read "+path+": "+file.errorString();return false;}
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject()) {error="Invalid JSON object: "+path+": "+parse.errorString();return false;}
    object=document.object();return true;
}
bool Request(const QString& projectRoot,ScriptCompileRequest& request,QString& error) {
    const QFileInfo project(projectRoot);
    if(!QDir::isAbsolutePath(projectRoot)||!project.isDir()||project.isSymLink()) {
        error="ProjectRoot must be an existing absolute project directory: "+projectRoot;return false;
    }
    const QDir root(project.canonicalFilePath());
    const QFileInfo assetsInfo(root.filePath("Assets"));
    if(!assetsInfo.isDir()||assetsInfo.isSymLink()) {error="Project has no supported Assets directory: "+assetsInfo.filePath();return false;}
    const QDir assets(assetsInfo.canonicalFilePath());
    QJsonObject assembly;
    if(!ReadObject(root.filePath("Info/GameLogic.json"),assembly,error)) {
        error+="\nOpen the project in GameSprout Editor once to prepare the GameLogic assembly identity.";return false;
    }
    const auto assemblyGuid=assembly.value("Guid").toString();
    if(assembly.value("Version").toInt()!=1||!Guid(assemblyGuid)) {
        error="Info/GameLogic.json must use the current Version 1 format and a valid assembly Guid.";return false;
    }
    request.AssetsRoot=assets.absolutePath().toStdString();
    request.BuildRoot=root.filePath("Library/ScriptBuild").toStdString();
    request.ArtifactRoot=root.filePath("Library/Artifact").toStdString();
    request.AssemblyGuid=assemblyGuid.toStdString();
    request.LockWaitMilliseconds=60000;
    std::set<QString> guids;
    QDirIterator entries(assets.absolutePath(),QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(entries.hasNext()) {
        if(Cancelled()) {error="GameLogic build cancelled.";return false;}
        const QFileInfo source(entries.next());
        if(!NativeSource(source.suffix().toLower())) continue;
        const auto canonical=source.canonicalFilePath();
        if(source.isSymLink()||canonical.isEmpty()||!canonical.startsWith(assets.absolutePath()+'/')) {
            error="Source must be a regular file inside Assets: "+source.filePath();return false;
        }
        QJsonObject metadata;
        if(!ReadObject(source.filePath()+".meta",metadata,error)) {
            error+="\nImport the source in GameSprout Editor before building. This tool never creates .meta files or GUIDs.";return false;
        }
        const auto guid=metadata.value("Guid").toString();
        if(metadata.value("Version").toInt()!=1||metadata.value("ImporterType").toString()!="Script"||!Guid(guid)) {
            error="Source .meta must use current Script metadata with a valid Guid: "+source.filePath()+".meta";return false;
        }
        if(!guids.insert(guid).second) {error="Duplicate source .meta Guid: "+source.filePath()+".meta";return false;}
        request.Sources.push_back({assets.relativeFilePath(source.filePath()).toStdString(),guid.toStdString()});
    }
    std::sort(request.Sources.begin(),request.Sources.end(),[](const auto& left,const auto& right){return left.Path<right.Path;});
    return true;
}
void Usage(QTextStream& output) {
    output<<"Usage: GameSproutScriptBuild --ProjectRoot=<absolute-project-directory>\n"
          <<"Builds GameLogic through GameSprout's native reflection and incremental compilation pipeline.\n";
}
}

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    QTextStream output(stdout),errors(stderr);
    std::signal(SIGINT,Interrupt);std::signal(SIGTERM,Interrupt);
    QString projectRoot;
    const auto arguments=application.arguments();
    for(int index=1;index<arguments.size();++index) {
        const auto argument=arguments[index];
        if(argument=="--help"||argument=="-h") {Usage(output);return 0;}
        if(!argument.startsWith("--ProjectRoot=")||!projectRoot.isEmpty()) {Usage(errors);return 2;}
        projectRoot=argument.mid(QStringLiteral("--ProjectRoot=").size());
    }
    if(projectRoot.isEmpty()) {Usage(errors);return 2;}
    try {
        ScriptCompileRequest request;QString error;
        if(!Request(projectRoot,request,error)) {errors<<error<<'\n';return Cancelled()?130:3;}
        ScriptCompiler compiler;
        ScriptCompileResult result;
        const bool success=compiler.Compile(request,result,Cancelled);
        if(!result.Diagnostics.empty()) errors<<QString::fromStdString(result.Diagnostics)<<'\n';
        if(!success) return Cancelled()?130:4;
        output<<(result.CacheHit?"GameLogic cache hit\n":"GameLogic build succeeded\n")
              <<"GameLogic library: "<<QString::fromStdString(result.LibraryPath)<<'\n'
              <<"GameLogic fingerprint: "<<QString::fromStdString(result.Fingerprint)<<'\n';
        return 0;
    } catch(const std::exception& exception) {errors<<"GameLogic build error: "<<exception.what()<<'\n';return 5;}
      catch(...) {errors<<"GameLogic build error: unknown exception\n";return 5;}
}

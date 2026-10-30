#include "ShaderCompileJob.h"
#include <QRegularExpression>
#include <set>

static QStringList PreprocessArguments(const ShaderCompileJob & job,const QString & main,const QString & output) {
    const char * profile=job.Request.Stage==eShaderStage::Vertex?"vs_6_0":job.Request.Stage==eShaderStage::Fragment?"ps_6_0":"cs_6_0";
    QStringList args{"-T",profile,"-E",QString::fromStdString(job.Request.EntryPoint),"-P","-Fi",output,
        "-I",QFileInfo(main).absolutePath(),"-I",QDir(job.Work).filePath("input")};
    args<<ShaderDefines(job.Request)<<main;return args;
}
static QString DecodeFilename(QString name) {
    name.replace("\\\\","\\").replace("\\\"","\"");return QDir::fromNativeSeparators(name);
}
static std::set<QString> SourceFiles(const QByteArray & preprocessed) {
    const QRegularExpression line(R"marker(^\s*#\s*(?:line\s+)?\d+\s+"((?:\\.|[^"\\])*)")marker",QRegularExpression::MultilineOption);
    auto matches=line.globalMatch(QString::fromUtf8(preprocessed));std::set<QString> result;
    while(matches.hasNext()) {
        const QString name=DecodeFilename(matches.next().captured(1));
        if(name.isEmpty() || (name.startsWith('<')&&name.endsWith('>'))) continue;
        result.insert(QFileInfo(name).canonicalFilePath());
    }
    return result;
}
bool PrepareShaderSource(ShaderCompileJob & job) {
    const QDir source(QDir(QString::fromStdString(job.Request.SourceRoot)).canonicalPath());
    const QDir snapshot(QDir(job.Work).filePath("input"));
    const QString main=snapshot.filePath(QString::fromStdString(job.Request.SourcePath));
    if(!job.Write(main,QByteArray::fromStdString(job.Request.Source))) return false;
    const QString discovery=QDir(job.Work).filePath("discovery.hlsl");
    QStringList args=PreprocessArguments(job,main,discovery);
    args<<"-I"<<QFileInfo(source.filePath(QString::fromStdString(job.Request.SourcePath))).absolutePath()<<"-I"<<source.absolutePath();
    if(!job.Run(job.Tools.DXC,args)) return false;
    QByteArray text;if(!job.Read(discovery,text)) return false;
    std::set<std::string> expected;
    for(const QString & path:SourceFiles(text)) {
        if(path==QFileInfo(main).canonicalFilePath()) continue;
        if(path.isEmpty() || !path.startsWith(source.canonicalPath()+"/")) return job.Fail("Shader include must be inside SourceRoot");
        const std::string relative=source.relativeFilePath(path).toStdString();
        QByteArray bytes;if(!job.Read(path,bytes) || !job.Write(snapshot.filePath(QString::fromStdString(relative)),bytes)) return false;
        expected.insert(relative);
        job.Result.Dependencies.push_back({relative,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString()});
    }
    job.Input=QDir(job.Work).filePath("flattened.hlsl");
    if(!job.Run(job.Tools.DXC,PreprocessArguments(job,main,job.Input)) || !job.Read(job.Input,text)) return false;
    std::set<std::string> observed;
    for(const QString & path:SourceFiles(text)) {
        if(path.isEmpty() || !path.startsWith(snapshot.canonicalPath()+"/")) return job.Fail("Use relative include paths inside Assets; compilation cannot read outside its snapshot");
        if(path!=QFileInfo(main).canonicalFilePath()) observed.insert(QDir(snapshot.canonicalPath()).relativeFilePath(path).toStdString());
    }
    return observed==expected || job.Fail("Shader include graph changed while creating the snapshot");
}

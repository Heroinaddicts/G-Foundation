#include "ShaderCompileJob.h"
#include <QProcess>
#include <sstream>

std::string ShaderJsonText(const Json::Value & value) {
    Json::StreamWriterBuilder writer;writer["indentation"]="";writer["emitUTF8"]=true;
    return Json::writeString(writer,value);
}
bool ParseShaderJson(const QByteArray & bytes, Json::Value & value) {
    Json::CharReaderBuilder reader;reader["rejectDupKeys"]=true;reader["failIfExtra"]=true;
    std::istringstream input(bytes.toStdString());std::string error;
    return Json::parseFromStream(reader,input,&value,&error);
}
bool ShaderCompileJob::Read(const QString & path,QByteArray & bytes) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return Fail("Cannot read shader input: "+path.toStdString());
    bytes=file.readAll();return file.error()==QFileDevice::NoError || Fail("Cannot read shader input: "+path.toStdString());
}
bool ShaderCompileJob::Write(const QString & path,const QByteArray & bytes) {
    if(!QDir().mkpath(QFileInfo(path).absolutePath())) return Fail("Cannot create shader working directory");
    QSaveFile file(path);file.setDirectWriteFallback(false);
    return (file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit()) || Fail("Cannot save shader working file: "+path.toStdString());
}
bool ShaderCompileJob::Run(const QString & program,const QStringList & arguments) {
    if(Cancelled()) return false;
    if(program.isEmpty() || !QFileInfo(program).isExecutable()) return Fail("SDL shader tool is missing: "+program.toStdString());
    QProcess process;process.setWorkingDirectory(Work);process.setProgram(program);process.setArguments(arguments);process.start();
    if(!process.waitForStarted(5000)) return Fail("Cannot start shader tool: "+process.errorString().toStdString());
    QByteArray diagnostics;
    for(;;) {
        const bool done=process.waitForFinished(100);
        diagnostics+=process.readAllStandardOutput();diagnostics+=process.readAllStandardError();
        if(diagnostics.size()>131072) diagnostics=diagnostics.right(131072);
        if(Cancelled()) {process.kill();process.waitForFinished(5000);return false;}
        if(done || process.state()==QProcess::NotRunning) break;
    }
    const QByteArray sourceRoot=QByteArray::fromStdString(Request.SourceRoot);
    for(const QString & root:{QDir(Work).canonicalPath(),QDir(Work).absolutePath()}) {
        diagnostics.replace((root+"/input").toUtf8(),sourceRoot);
        diagnostics.replace((root+"/flattened.hlsl").toUtf8(),sourceRoot+"/"+QByteArray::fromStdString(Request.SourcePath));
    }
    Result.Diagnostics+=diagnostics.toStdString();
    if(Result.Diagnostics.size()>262144) Result.Diagnostics.erase(0,Result.Diagnostics.size()-262144);
    return (process.exitStatus()==QProcess::NormalExit && process.exitCode()==0)
        || Fail("Shader compiler failed ("+QFileInfo(program).fileName().toStdString()+"): "+diagnostics.toStdString());
}
QStringList ShaderDefines(const ShaderCompileRequest & request) {
    QStringList values;
    for(const auto & define:request.Defines) {
        QString value=QString::fromStdString(define);if(!value.contains('=')) value+="=1";
        values<<"-D"<<value;
    }
    return values;
}

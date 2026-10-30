#pragma once
#include "AssetImportJob.h"
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

namespace AssetToolProcess {
inline QString Find(const QString & name) {
#if defined(_WIN32)
    const QString executable=name+".exe";
#else
    const QString executable=name;
#endif
    const QDir application(QCoreApplication::applicationDirPath());
    for (const QString & path : {application.filePath("local_tools/shaders/bin/"+executable),
        application.filePath("local_tools/"+executable),application.filePath(executable)})
        if (QFileInfo(path).isExecutable()) return path;
    return QStandardPaths::findExecutable(executable);
}
inline bool Run(AssetImportJob & job,const QString & program,const QStringList & arguments) {
    if (program.isEmpty()) return job.Fail("Required asset compiler is not installed or deployed");
    QProcess process;
    process.setProgram(program);process.setArguments(arguments);process.start();
    if (!process.waitForStarted(5000)) return job.Fail("Cannot start asset compiler: "+process.errorString().toStdString());
    QByteArray diagnostics;
    for (;;) {
        const bool done=process.waitForFinished(100);
        diagnostics+=process.readAllStandardOutput();diagnostics+=process.readAllStandardError();
        if(diagnostics.size()>65536) diagnostics=diagnostics.right(65536);
        if(job.Cancelled()) {process.kill();process.waitForFinished(5000);return false;}
        if(done || process.state()==QProcess::NotRunning) break;
    }
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
        if(!job.Stage.empty()) {
            const QDir stage(QString::fromStdString(job.Stage));
            const QByteArray assets=QByteArray::fromStdString(job.Request.AssetsRoot);
            diagnostics.replace((stage.canonicalPath()+"/shader-input").toUtf8(),assets);
            diagnostics.replace((stage.absolutePath()+"/shader-input").toUtf8(),assets);
        }
        return job.Fail("Asset compiler failed ("+QFileInfo(program).fileName().toStdString()+"): "+diagnostics.toStdString());
    }
    return true;
}
inline bool Read(const QString & path,QByteArray & bytes) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)) return false;
    bytes=file.readAll();return file.error()==QFileDevice::NoError;
}
}

#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <string>

// Value-only input captured by CodeEditor before submitting the worker task.
// Sources are paths relative to Assets; they refer to the authored files.
struct CodeEditorProjectRequest {
    QString ProjectRoot;
    QString RuntimeSDKDirectory;
    QString CMakeProgram;
    QString CompilerProgram;
    QString BuildToolProgram;
    QStringList BuildToolArguments;
    QStringList Sources;
    QString Configuration;
    QString Architectures;
    QString Sysroot;
    QString DeploymentTarget;
    QString BaseFlags;
    QString ConfigurationFlags;
};
struct CodeEditorProjectResult {
    QString ProjectPath;
    QString Diagnostics;
    bool Changed=false;
};

// Platform integration is deliberately independent of ScriptCompiler internals.
// Generate/Open are blocking worker operations, never a View Update callback.
class CodeEditorBackend {
public:
    virtual ~CodeEditorBackend() = default;
    virtual bool Generate(const CodeEditorProjectRequest&,CodeEditorProjectResult&,
                          std::string& error,const std::function<bool()>& cancelled) const = 0;
    virtual bool Open(const QString& projectPath,const QString& absoluteSourcePath,int line,
                      std::string& error,const std::function<bool()>& cancelled) const = 0;
    static CodeEditorBackend* Create();
};

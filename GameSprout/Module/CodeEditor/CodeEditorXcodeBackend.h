#pragma once
#include "CodeEditorBackend.h"

class CodeEditorXcodeBackend final : public CodeEditorBackend {
public:
    bool Generate(const CodeEditorProjectRequest&,CodeEditorProjectResult&,
                  std::string& error,const std::function<bool()>& cancelled) const override;
    bool Open(const QString& projectPath,const QString& absoluteSourcePath,int line,
              std::string& error,const std::function<bool()>& cancelled) const override;
    // Also used by the integration test without launching a GUI application.
    static QStringList OpenArguments(const QString& projectPath,const QString& sourcePath,int line);
};

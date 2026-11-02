#include "CodeEditorBackend.h"
#if defined(Q_OS_MACOS)
#include "CodeEditorXcodeBackend.h"
#else
namespace {
class UnsupportedCodeEditorBackend final : public CodeEditorBackend {
public:
    bool Generate(const CodeEditorProjectRequest&,CodeEditorProjectResult&,
                  std::string& error,const std::function<bool()>&) const override {
        error="当前平台尚未接入外部代码编辑器。";return false;
    }
    bool Open(const QString&,const QString&,int,std::string& error,
              const std::function<bool()>&) const override {
        error="当前平台尚未接入外部代码编辑器。";return false;
    }
};
}
#endif
CodeEditorBackend* CodeEditorBackend::Create() {
#if defined(Q_OS_MACOS)
    return new CodeEditorXcodeBackend();
#else
    return new UnsupportedCodeEditorBackend();
#endif
}

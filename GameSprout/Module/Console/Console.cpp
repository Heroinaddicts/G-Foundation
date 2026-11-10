#include "Console.h"
#include "IProject.h"

#include <QDir>
#include <QFileInfo>
#include <utility>

namespace {
QString RelativeSourcePath(const QString& root, const QString& file) {
    if (root.isEmpty()) return {};
    const QString relative = QDir(root).relativeFilePath(file);
    if (relative == "." || relative == ".." || relative.startsWith("../") || QDir::isAbsolutePath(relative)) return {};
    return relative;
}
}

bool Console::Initialize(GameSproutModuleApi * const) {
    return true;
}

bool Console::Launch(GameSproutModuleApi * const api) {
    if (_Runtime) return true;
    if (!api || !api->_EditorApi) return false;
    const auto* editor = api->_EditorApi;
    _View = editor->GetGameSproutView<IConsoleUI>("ConsoleUI");
    auto* runtime = editor->_Runtime;
    if (!_View || !runtime) return false;

    if (auto* project = editor->GetGameSproutModule<IProject>("Project"); project && !project->GetProjectRoot().empty()) {
        const QDir root(QString::fromStdString(project->GetProjectRoot()));
        _AssetsRoot = root.absoluteFilePath("Assets");
        _ScriptSources = root.absoluteFilePath("Library/ScriptBuild/Sources");
        _CanonicalScriptSources = QFileInfo(_ScriptSources).canonicalFilePath();
    }
    if (!runtime->RegisterLogListener(this)) return false;
    _Runtime = runtime;
    return true;
}

void Console::Release(GameSproutModuleApi * const) {
    if (_Runtime) _Runtime->UnregisterLogListener(this);
    _Runtime = nullptr;
    _View = nullptr;
    _AssetsRoot.clear();
    _ScriptSources.clear();
    _CanonicalScriptSources.clear();
}

void Console::ConsoleLog(const char * file, const int line, const eConsoleLog level, const std::string & log) {
    if (_View) _View->ConsoleLog(file, line, level, log);
}

void Console::OnRuntimeLog(const GameSproutRuntime::RuntimeLogRecord& record) {
    using GameSproutRuntime::RuntimeLogLevel;
    eConsoleLog level;
    switch (record.Level) {
    case RuntimeLogLevel::Debug: level = eConsoleLog::Debug; break;
    case RuntimeLogLevel::Trace: level = eConsoleLog::Trace; break;
    case RuntimeLogLevel::Warning: level = eConsoleLog::Warning; break;
    case RuntimeLogLevel::Error: level = eConsoleLog::Error; break;
    default: return;
    }
    const std::string file = SourceFile(record.File);
    const int line = std::in_range<int>(record.Line) ? static_cast<int>(record.Line) : 0;
    ConsoleLog(file.c_str(), line, level, record.Message);
}

std::string Console::SourceFile(const std::string& file) {
    if (file.empty() || _ScriptSources.isEmpty()) return file;
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(QString::fromStdString(file)));
    QString relative;
    if (QDir::isAbsolutePath(path)) {
        relative = RelativeSourcePath(_ScriptSources, path);
        if (relative.isEmpty()) {
            // The first GameLogic build can create Sources after Console has launched.
            if (_CanonicalScriptSources.isEmpty()) _CanonicalScriptSources = QFileInfo(_ScriptSources).canonicalFilePath();
            relative = RelativeSourcePath(_CanonicalScriptSources, path);
        }
    } else if (path != "." && path != ".." && !path.startsWith("../") &&
               QFileInfo(QDir(_ScriptSources).filePath(path)).isFile()) {
        relative = path;
    }
    return relative.isEmpty() ? file : QDir(_AssetsRoot).filePath(relative).toStdString();
}

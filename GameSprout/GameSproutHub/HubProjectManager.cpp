#include "HubProjectManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSaveFile>

QString HubProjectManager::HistoryPath() {
    return QDir::home().filePath(QStringLiteral(".GameSproutHub/Projects/porjects.json"));
}

QString HubProjectManager::EditorExecutable() {
    const QDir directory(QCoreApplication::applicationDirPath());
#if defined(Q_OS_MACOS)
    return QDir::cleanPath(directory.filePath(
        QStringLiteral("../../../GameSprout.app/Contents/MacOS/GameSprout")));
#elif defined(Q_OS_WIN)
    return directory.filePath(QStringLiteral("GameSprout.exe"));
#else
    return directory.filePath(QStringLiteral("GameSprout"));
#endif
}

bool HubProjectManager::Load(QString& error) {
    error.clear();
    _CanSaveProjects = false;
    QFile file(HistoryPath());
    if (!file.exists()) {
        _Projects.clear();
        _CanSaveProjects = true;
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取项目列表：%1").arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = QStringLiteral("无法读取项目列表：%1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = QStringLiteral("项目列表 JSON 无效：%1").arg(parseError.errorString());
        return false;
    }
    if (!document.isObject() ||
        !document.object().value(QStringLiteral("projects")).isArray()) {
        error = QStringLiteral("项目列表格式无效，需要 projects 数组。");
        return false;
    }

    QStringList projects;
    const QJsonArray entries = document.object().value(QStringLiteral("projects")).toArray();
    for (const QJsonValue& entry : entries) {
        const QString path = entry.toObject().value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
            error = QStringLiteral("项目列表包含无效的项目目录。");
            return false;
        }
        const QFileInfo info(path);
        const QString root = info.isDir()
            ? info.canonicalFilePath() : QDir::cleanPath(path);
        if (!projects.contains(root)) {
            projects.push_back(root);
        }
    }
    _Projects = projects;
    _CanSaveProjects = true;
    return true;
}

bool HubProjectManager::Save(QString& error) const {
    if (!_CanSaveProjects) {
        error = QStringLiteral("原项目列表读取失败，未覆盖原文件。请检查 %1。")
                    .arg(HistoryPath());
        return false;
    }
    if (!QDir().mkpath(QFileInfo(HistoryPath()).absolutePath())) {
        error = QStringLiteral("无法创建项目列表目录。");
        return false;
    }
    QJsonArray entries;
    for (const QString& root : _Projects) {
        QJsonObject entry;
        entry.insert(QStringLiteral("name"), QDir(root).dirName());
        entry.insert(QStringLiteral("path"), root);
        entries.append(entry);
    }
    QJsonObject document;
    document.insert(QStringLiteral("projects"), entries);
    const QByteArray data = QJsonDocument(document).toJson(QJsonDocument::Indented);
    QSaveFile file(HistoryPath());
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit()) {
        error = QStringLiteral("无法保存项目列表：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool HubProjectManager::CreateProject(const QString& name, const QString& parentDirectory,
                                     QString& projectRoot, QString& error) {
    error.clear();
    projectRoot.clear();
    const QString projectName = name.trimmed();
    if (projectName.isEmpty() || projectName == QStringLiteral(".") ||
        projectName == QStringLiteral("..") || projectName.endsWith(QLatin1Char('.'))) {
        error = QStringLiteral("请输入有效的项目名称。");
        return false;
    }
    for (const QChar character : projectName) {
        if (character.unicode() < 32 || QStringLiteral("\\/:*?\"<>|").contains(character)) {
            error = QStringLiteral("项目名称不能包含路径分隔符或 \\ / : * ? \" < > | 等字符。");
            return false;
        }
    }

    const QFileInfo location(parentDirectory);
    if (!location.isDir() || location.canonicalFilePath().isEmpty()) {
        error = QStringLiteral("保存位置不存在，请选择已有目录。");
        return false;
    }
    const QDir parent(location.canonicalFilePath());
    const QString root = parent.filePath(projectName);
    const QFileInfo target(root);
    if (target.exists() || target.isSymLink()) {
        error = QStringLiteral("目标已存在，不能覆盖：%1").arg(root);
        return false;
    }
    if (!parent.mkdir(projectName)) {
        error = QStringLiteral("无法创建项目目录：%1").arg(root);
        return false;
    }

    const QDir projectDirectory(root);
    if (!projectDirectory.mkdir(QStringLiteral("Info"))) {
        error = QStringLiteral("无法创建项目 Info 目录：%1").arg(root);
        parent.rmdir(projectName);
        return false;
    }

    QJsonObject projectInfo;
    projectInfo.insert(QStringLiteral("name"), projectName);
    QJsonObject document;
    document.insert(QStringLiteral("Project"), projectInfo);
    const QByteArray data = QJsonDocument(document).toJson(QJsonDocument::Indented);
    bool saved = false;
    {
        QSaveFile file(projectDirectory.filePath(QStringLiteral("Info/project.json")));
        saved = file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
                file.commit();
        if (!saved) {
            error = QStringLiteral("无法写入 Info/project.json：%1").arg(file.errorString());
        }
    }
    if (!saved) {
        projectDirectory.rmdir(QStringLiteral("Info"));
        parent.rmdir(projectName);
        return false;
    }

    projectRoot = root;
    if (!RememberProject(root, error)) {
        error.prepend(QStringLiteral("项目已创建，但历史记录未保存："));
    }
    return true;
}

bool HubProjectManager::RememberProject(const QString& root, QString& error) {
    _Projects.removeAll(root);
    _Projects.prepend(root);
    return Save(error);
}

bool HubProjectManager::RemoveProjectRecord(const QString& root, QString& error) {
    error.clear();
    const qsizetype index = _Projects.indexOf(root);
    if (index < 0) {
        error = QStringLiteral("项目记录不存在：%1").arg(root);
        return false;
    }
    const QStringList previous = _Projects;
    _Projects.removeAt(index);
    if (!Save(error)) {
        _Projects = previous;
        return false;
    }
    return true;
}

bool HubProjectManager::OpenProject(const QString& directory, QString& error, bool isNewProject) {
    error.clear();
    const QFileInfo project(directory);
    if (!project.exists() || !project.isDir()) {
        error = QStringLiteral("项目目录不存在：%1").arg(directory);
        return false;
    }
    const QString root = project.canonicalFilePath();
    if (root.isEmpty()) {
        error = QStringLiteral("无法访问项目目录：%1").arg(directory);
        return false;
    }
    const QString executable = EditorExecutable();
    const QFileInfo editor(executable);
    if (!editor.isFile() || !editor.isExecutable()) {
        error = QStringLiteral("找不到 GameSprout 编辑器：%1。请将编辑器与 Hub 放在同一目录。")
                    .arg(executable);
        return false;
    }
    QProcess process;
    process.setProgram(executable);
    QStringList arguments{QStringLiteral("--ProjectRoot=") + root};
    if (isNewProject) {
        arguments.append(QStringLiteral("--IsNewProject"));
    }
    process.setArguments(arguments);
    process.setWorkingDirectory(root);
    if (!process.startDetached()) {
        error = QStringLiteral("无法启动 GameSprout：%1").arg(process.errorString());
        return false;
    }
    if (!RememberProject(root, error)) {
        error.prepend(QStringLiteral("编辑器已启动，但历史记录未保存："));
    }
    return true;
}

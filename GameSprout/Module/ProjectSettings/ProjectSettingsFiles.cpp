#include "ProjectSettings.h"
#include "ProjectSettingsFormat.h"
#include "IProject.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

bool ProjectSettings::Load(std::string& error) {
    error.clear();
    const QFileInfo project(QString::fromStdString(_Project->GetProjectRoot()));
    if (!project.isDir() || project.canonicalFilePath().isEmpty()) { error = "项目目录不存在。"; return false; }
    _Root = project.canonicalFilePath();
    const auto folder = QDir(_Root).filePath("ProjectSettings");
    const QFileInfo info(folder);
    if (info.isSymLink() || (info.exists() && !info.isDir())) { error = "ProjectSettings 必须是项目内的实际目录。"; return false; }
    if (!QDir(_Root).mkpath("ProjectSettings")) { error = "无法创建 ProjectSettings 目录。"; return false; }
    _Path = QDir(folder).filePath("ProjectSettings.json");
    const QFileInfo fileInfo(_Path);
    if (fileInfo.isSymLink() || (fileInfo.exists() && !fileInfo.isFile())) { error = "项目设置必须是普通文件。"; return false; }
    GameSproutRuntime::RuntimeProjectSettings data;
    QByteArray bytes;
    if (fileInfo.exists()) {
        QFile file(_Path);
        if (!file.open(QIODevice::ReadOnly)) { error = "无法读取项目设置：" + file.errorString().toStdString(); return false; }
        if (file.size() > 64 * 1024 * 1024) { error = "项目设置超过 64 MiB，拒绝读取。"; return false; }
        bytes = file.readAll();
        if (file.error() != QFileDevice::NoError) { error = "项目设置读取失败：" + file.errorString().toStdString(); return false; }
        if (!ProjectSettingsFormat::Decode(bytes, data, error)) { error = "项目设置无效：" + error; return false; }
    } else bytes = ProjectSettingsFormat::Encode(data);
    if (!_Runtime->CanApplyProjectSettings(data, error)) return false;
    if (!fileInfo.exists() && !Write(bytes, error)) return false;
    if (!_Runtime->SetProjectSettings(data, error)) return false;
    _Settings = std::move(data); _DiskSnapshot = bytes; return true;
}
bool ProjectSettings::Write(const QByteArray& bytes, std::string& error) const {
    if (QFileInfo(_Path).isSymLink() || QFileInfo(QFileInfo(_Path).absolutePath()).isSymLink()) {
        error = "禁止通过符号链接写入项目设置。"; return false;
    }
    QSaveFile file(_Path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = "项目设置保存失败：" + file.errorString().toStdString(); return false;
    }
    return true;
}

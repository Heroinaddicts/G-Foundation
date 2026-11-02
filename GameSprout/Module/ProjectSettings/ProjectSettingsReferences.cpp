#include "ProjectSettings.h"
#include "ProjectSettingsFormat.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>
#include <system_error>
#include <vector>

namespace {
bool ContainsRemovedTag(const Json::Value& value, const std::set<std::uint64_t>& removed) {
    if (value.isObject()) {
        for (const auto* key : {"TagID", "RootTagID"})
            if (value[key].isUInt64() && removed.count(value[key].asUInt64())) return true;
        // Prefab property patches may represent the field as a path rather
        // than an object key. Both saved baselines and overrides retain IDs.
        const auto& path = value["Path"];
        if (path.isArray() && !path.empty() && path[path.size()-1].isString() &&
            (path[path.size()-1].asString() == "TagID" || path[path.size()-1].asString() == "RootTagID"))
            for (const auto* key : {"Before", "After", "Value"})
                if (value[key].isUInt64() && removed.count(value[key].asUInt64())) return true;
        for (const auto& key : value.getMemberNames()) if (ContainsRemovedTag(value[key], removed)) return true;
    } else if (value.isArray()) for (const auto& item : value) if (ContainsRemovedTag(item, removed)) return true;
    return false;
}
}
bool ProjectSettings::CheckSourceReferences(const std::set<std::uint64_t>& removed, std::string& error) const {
    if (removed.empty()) return true;
    const auto assets = QDir(_Root).filePath("Assets");
    const QFileInfo assetsInfo(assets);
    if (assetsInfo.isSymLink()) { error = "无法通过符号链接检查 Assets 中的 Tag 引用。"; return false; }
    if (!assetsInfo.exists()) return true;
    if (!assetsInfo.isDir()) { error = "无法检查 Assets 中的 Tag 引用。"; return false; }
    // AssetsManager deliberately ignores symbolic links. Enumerate actual
    // directories explicitly with error_code: QDirIterator can silently skip
    // an inaccessible subtree, which would make "unreferenced" a false result.
    const auto pathText = [](const std::filesystem::path& path) {
        const auto utf8 = path.u8string();
        return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()), static_cast<qsizetype>(utf8.size()));
    };
    const auto enumerationFailed = [&](const std::filesystem::path& path, const std::error_code& failure) {
        error = "无法完整检查目录中的 Tag 引用：" + pathText(path).toStdString();
        if (failure) error += "：" + failure.message();
        return false;
    };
    const auto assetsBytes = assets.toUtf8();
    std::vector<std::filesystem::path> pending{
        std::filesystem::u8path(assetsBytes.constData(), assetsBytes.constData() + assetsBytes.size())
    };
    while (!pending.empty()) {
        const auto directory = std::move(pending.back());
        pending.pop_back();
        std::error_code failure;
        std::filesystem::directory_iterator current(directory, std::filesystem::directory_options::none, failure);
        if (failure) return enumerationFailed(directory, failure);
        const std::filesystem::directory_iterator end;
        while (current != end) {
            const auto path = current->path();
            const auto status = current->symlink_status(failure);
            if (failure || !std::filesystem::exists(status)) return enumerationFailed(path, failure);
            if (!std::filesystem::is_symlink(status)) {
                if (std::filesystem::is_directory(status)) pending.push_back(path);
                else {
                    const auto filename = pathText(path);
                    const auto suffix = QFileInfo(filename).suffix().toLower();
                    if (suffix == "scene" || suffix == "prefab") {
                        if (!std::filesystem::is_regular_file(status)) return enumerationFailed(path, {});
                        QFile file(filename);
                        if (!file.open(QIODevice::ReadOnly)) {
                            error = "无法检查资源中的 Tag 引用：" + filename.toStdString(); return false;
                        }
                        if (file.size() > 64 * 1024 * 1024) {
                            error = "资源超过 64 MiB，无法安全检查 Tag 引用：" + filename.toStdString(); return false;
                        }
                        const auto bytes = file.readAll();
                        Json::Value source;
                        std::string parseError;
                        if (file.error() != QFileDevice::NoError || !ProjectSettingsFormat::Parse(bytes, source, parseError) || !source.isObject()) {
                            error = "无法确认资源的 Tag 引用，请先修复文件：" + filename.toStdString(); return false;
                        }
                        if (ContainsRemovedTag(source, removed)) {
                            error = "Tag 仍被资源引用，不能删除：Assets/" + QDir(assets).relativeFilePath(filename).toStdString(); return false;
                        }
                    }
                }
            }
            current.increment(failure);
            if (failure) return enumerationFailed(directory, failure);
        }
    }
    return true;
}

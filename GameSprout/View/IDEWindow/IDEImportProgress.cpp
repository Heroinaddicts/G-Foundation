#include "IDEWindow.h"
#include "IDELoadingOverlay.h"
#include <limits>

uint64_t IDEWindow::BeginImportProgress(uint64_t total, const std::string& description) {
    if (!_LoadingOverlay || total == 0 || _NextImportProgress == (std::numeric_limits<uint64_t>::max)()) return 0;
    const uint64_t token = ++_NextImportProgress;
    _ImportProgress.emplace(token, ImportProgress{total, 0, description});
    _HadImportProgress = true;
    RefreshLoadingProgress();
    return token;
}

bool IDEWindow::UpdateImportProgress(uint64_t token, uint64_t completed, const std::string& path) {
    const auto found = _ImportProgress.find(token);
    if (found == _ImportProgress.end() || completed < found->second.Completed || completed > found->second.Total) return false;
    found->second.Completed = completed;
    found->second.Description = path;
    RefreshLoadingProgress();
    return true;
}

bool IDEWindow::EndImportProgress(uint64_t token) {
    const auto found = _ImportProgress.find(token);
    if (found == _ImportProgress.end() || found->second.Completed != found->second.Total) return false;
    _ImportProgress.erase(found);
    RefreshLoadingProgress();
    return true;
}

void IDEWindow::RefreshLoadingProgress() {
    if (!_LoadingOverlay || _LoadingRefCount <= 0) return;
    if (!_ImportProgress.empty()) {
        const ImportProgress& progress = _ImportProgress.begin()->second;
        _LoadingOverlay->SetTitle(QStringLiteral("资源导入"));
        _LoadingOverlay->SetDescription(QString::fromStdString(progress.Description));
        _LoadingOverlay->SetProgress(progress.Completed, progress.Total);
    } else {
        if (_HadImportProgress) {
            _LoadingOverlay->SetTitle(QStringLiteral("资源初始化"));
            _LoadingOverlay->SetDescription(QStringLiteral("资源已处理，正在完成其他初始化工作…"));
        } else {
            _LoadingOverlay->SetTitle(QString::fromStdString(_LoadingTitle));
            _LoadingOverlay->SetDescription(QString::fromStdString(_LoadingDescription));
        }
        _LoadingOverlay->SetIndeterminateProgress();
    }
}

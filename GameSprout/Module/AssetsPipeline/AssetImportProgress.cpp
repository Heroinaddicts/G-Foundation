#include "AssetsPipeline.h"

uint64_t AssetsPipeline::BeginImportBatch(uint64_t count) {
    if (!_IDE || count == 0) return 0;
    const uint64_t batch = ++_NextImportBatch;
    const uint64_t token = _IDE->BeginImportProgress(count, "准备校验资源");
    _ImportBatches.emplace(batch, ImportBatch{token, count, 0, 0, true});
    _BatchRegistration.push_back(batch);
    // 提交期间也持有遮罩，首项同步拒绝不能清除尚未登记完的进度批次。
    _IDE->Loading("资源检查", "登记导入任务");
    return batch;
}

void AssetsPipeline::EndImportBatch(uint64_t batch) {
    const auto active = _ImportBatches.find(batch);
    if (active == _ImportBatches.end() || !active->second.RegistrationOpen) return;
    for (auto item = _BatchRegistration.begin(); item != _BatchRegistration.end(); ++item) {
        if (*item == batch) { _BatchRegistration.erase(item); break; }
    }
    ImportBatch & progress = active->second;
    progress.RegistrationOpen = false;
    progress.Completed += progress.Total - progress.Registered;
    progress.Registered = progress.Total; // 异常中止登记的剩余项按未提交取消结算。
    _IDE->UpdateImportProgress(progress.Token, progress.Completed, "等待资源处理完成");
    if (progress.Completed == progress.Total) {
        _IDE->EndImportProgress(progress.Token);
        _ImportBatches.erase(active);
    }
    _IDE->Unloading();
}

uint64_t AssetsPipeline::RegisterImportProgress(const std::string & path) {
    if (!_BatchRegistration.empty()) {
        const auto active = _ImportBatches.find(_BatchRegistration.back());
        if (active != _ImportBatches.end() && active->second.Registered < active->second.Total) {
            ++active->second.Registered;
            return active->first;
        }
    }
    const uint64_t batch = ++_NextImportBatch;
    const uint64_t token = _IDE->BeginImportProgress(1, path);
    _ImportBatches.emplace(batch, ImportBatch{token, 1, 1, 0, false});
    return batch;
}

void AssetsPipeline::FinishImportProgress(uint64_t batch, const std::string & path) {
    const auto active = _ImportBatches.find(batch);
    if (active == _ImportBatches.end()) return;
    ImportBatch & progress = active->second;
    if (progress.Completed < progress.Total) ++progress.Completed;
    _IDE->UpdateImportProgress(progress.Token, progress.Completed, path);
    if (!progress.RegistrationOpen && progress.Completed == progress.Total) {
        _IDE->EndImportProgress(progress.Token);
        _ImportBatches.erase(active);
    }
}

void AssetsPipeline::ClearImportProgress() {
    for (const auto & active : _ImportBatches) {
        _IDE->UpdateImportProgress(active.second.Token, active.second.Total, "导入已取消");
        _IDE->EndImportProgress(active.second.Token);
        if (active.second.RegistrationOpen) _IDE->Unloading();
    }
    _ImportBatches.clear();
    _BatchRegistration.clear();
}
